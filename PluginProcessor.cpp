#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Drift depth at 100%: ~0.4 dB of gain wander, ~0.3 dB of L/R balance wander
    constexpr float kMaxGainDrift = 0.045f;
    constexpr float kMaxPanDrift  = 0.03f;

    // Asymmetry for the even-harmonic branch of the curve
    constexpr float kBias     = 0.35f;
    constexpr float kBiasTanh = 0.33638f;   // tanh (0.35)
    constexpr float kEvenNorm = 0.8996f;    // keeps peak levels close to the odd branch

    /** Hybrid saturator.
        - odd branch : tanh (x)                -> symmetric, odd harmonics (3rd, 5th...)
        - even branch: tanh (x + bias) - tanh (bias) -> asymmetric, adds even harmonics (2nd, 4th...)
        The DC offset created by the asymmetric branch is removed by the DC blocker. */
    inline float shape (float x, float character) noexcept
    {
        const float odd  = std::tanh (x);
        const float even = (std::tanh (x + kBias) - kBiasTanh) * kEvenNorm;
        return (odd + character * (even - odd)) * (1.0f + 0.25f * character);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PulseAudioProcessor::createParameterLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;

    auto withUnit = [] (const String& unit, int decimals)
    {
        return AudioParameterFloatAttributes().withStringFromValueFunction (
            [unit, decimals] (float v, int) { return String (v, decimals) + " " + unit; });
    };

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::drive, 1 }, "Drive",
                                                       Range (0.0f, 30.0f, 0.01f), 6.0f, withUnit ("dB", 1)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::character, 1 }, "Character",
                                                       Range (0.0f, 100.0f, 0.1f), 35.0f, withUnit ("%", 0)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::drift, 1 }, "Drift",
                                                       Range (0.0f, 100.0f, 0.1f), 30.0f, withUnit ("%", 0)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                                       Range (0.0f, 100.0f, 0.1f), 100.0f, withUnit ("%", 0)));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                                       Range (-24.0f, 12.0f, 0.01f), 0.0f, withUnit ("dB", 1)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::noiseOn, 1 }, "Noise Floor", false));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::noiseLevel, 1 }, "Noise Level",
                                                       Range (-96.0f, -60.0f, 0.01f), -80.0f, withUnit ("dB", 0)));

    return layout;
}

//==============================================================================
PulseAudioProcessor::PulseAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PULSE_STATE", createParameterLayout())
{
    driveParam      = apvts.getRawParameterValue (ParamIDs::drive);
    characterParam  = apvts.getRawParameterValue (ParamIDs::character);
    driftParam      = apvts.getRawParameterValue (ParamIDs::drift);
    mixParam        = apvts.getRawParameterValue (ParamIDs::mix);
    outputParam     = apvts.getRawParameterValue (ParamIDs::output);
    noiseOnParam    = apvts.getRawParameterValue (ParamIDs::noiseOn);
    noiseLevelParam = apvts.getRawParameterValue (ParamIDs::noiseLevel);

    rng.setSeedRandomly();
}

//==============================================================================
void PulseAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    // Oversampling + latency compensation
    oversampler.initProcessing ((size_t) maxBlockSize);
    oversampler.reset();
    const int latency = juce::roundToInt (oversampler.getLatencyInSamples());
    setLatencySamples (latency);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    dryDelay.prepare (spec);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    dryBuffer.setSize (2, maxBlockSize);
    dryBuffer.clear();
    gainBufL.assign ((size_t) maxBlockSize, 1.0f);
    gainBufR.assign ((size_t) maxBlockSize, 1.0f);

    // Smoothers
    const double osRate = sampleRate * (double) oversampler.getOversamplingFactor();

    driveSm.reset (sampleRate, 0.02);
    mixSm.reset (sampleRate, 0.02);
    outputSm.reset (sampleRate, 0.02);
    noiseSm.reset (sampleRate, 0.05);
    driftSm.reset (sampleRate, 0.05);
    characterSm.reset (osRate, 0.02);

    driveSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (driveParam->load()));
    mixSm.setCurrentAndTargetValue (mixParam->load() * 0.01f);
    outputSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    driftSm.setCurrentAndTargetValue (driftParam->load() * 0.01f);
    characterSm.setCurrentAndTargetValue (characterParam->load() * 0.01f);
    noiseSm.setCurrentAndTargetValue (noiseOnParam->load() > 0.5f
                                          ? juce::Decibels::decibelsToGain (noiseLevelParam->load())
                                          : 0.0f);

    // Drift: ~1.2 s smoothing, new random target every 1.5 - 4 s
    driftCoef   = 1.0f - std::exp (-1.0f / (1.2f * (float) sampleRate));
    driftMinLen = (int) (1.5 * sampleRate);
    driftMaxLen = (int) (4.0 * sampleRate);
    walkerL   = {};
    walkerR   = {};
    walkerPan = {};
    pinkL = {};
    pinkR = {};

    // DC blocker, ~5 Hz corner at the oversampled rate
    dcR = std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) osRate);
    for (int ch = 0; ch < 2; ++ch)
        dcX1[ch] = dcY1[ch] = 0.0f;
}

void PulseAudioProcessor::releaseResources()
{
    oversampler.reset();
}

bool PulseAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void PulseAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
        return;

    // Update smoother targets once per block
    driveSm.setTargetValue (juce::Decibels::decibelsToGain (driveParam->load()));
    characterSm.setTargetValue (characterParam->load() * 0.01f);
    driftSm.setTargetValue (driftParam->load() * 0.01f);
    mixSm.setTargetValue (mixParam->load() * 0.01f);
    outputSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    noiseSm.setTargetValue (noiseOnParam->load() > 0.5f
                                ? juce::Decibels::decibelsToGain (noiseLevelParam->load())
                                : 0.0f);

    // Process in chunks no larger than what we prepared for (hosts may exceed it)
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, total - start);
        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, start, n);
        processChunk (sub);
    }
}

void PulseAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    // 1) Latency-matched dry copy
    dryBuffer.copyFrom (0, 0, buffer, 0, 0, n);
    dryBuffer.copyFrom (1, 0, buffer, 1, 0, n);
    {
        juce::dsp::AudioBlock<float> dryBlock (dryBuffer.getArrayOfWritePointers(), 2, (size_t) n);
        dryDelay.process (juce::dsp::ProcessContextReplacing<float> (dryBlock));
    }

    // 2) Drive + generate drift gains (applied after saturation so drift stays audible)
    for (int i = 0; i < n; ++i)
    {
        const float amt = driftSm.getNextValue();
        const float dL  = walkerL.next   (rng, driftCoef, driftMinLen, driftMaxLen);
        const float dR  = walkerR.next   (rng, driftCoef, driftMinLen, driftMaxLen);
        const float dP  = walkerPan.next (rng, driftCoef, driftMinLen, driftMaxLen);

        const float pan = dP * kMaxPanDrift * amt;
        gainBufL[(size_t) i] = (1.0f + dL * kMaxGainDrift * amt) * (1.0f - pan);
        gainBufR[(size_t) i] = (1.0f + dR * kMaxGainDrift * amt) * (1.0f + pan);

        const float g = driveSm.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    // 3) 4x oversampled saturation (anti-aliased by the half-band filters)
    juce::dsp::AudioBlock<float> block (buffer);
    auto upBlock = oversampler.processSamplesUp (block);

    float* up[2] = { upBlock.getChannelPointer (0), upBlock.getChannelPointer (1) };
    const size_t numUp = upBlock.getNumSamples();

    for (size_t i = 0; i < numUp; ++i)
    {
        const float c = characterSm.getNextValue();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float y = shape (up[ch][i], c);

            // DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
            const float out = y - dcX1[ch] + dcR * dcY1[ch];
            dcX1[ch] = y;
            dcY1[ch] = out;
            up[ch][i] = out;
        }
    }

    oversampler.processSamplesDown (block);

    // 4) Drift gains, mix, output, optional noise floor
    const float* dryL = dryBuffer.getReadPointer (0);
    const float* dryR = dryBuffer.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float m  = mixSm.getNextValue();
        const float og = outputSm.getNextValue();
        const float ng = noiseSm.getNextValue();

        const float wetL = l[i] * gainBufL[(size_t) i];
        const float wetR = r[i] * gainBufR[(size_t) i];

        float outL = (dryL[i] * (1.0f - m) + wetL * m) * og;
        float outR = (dryR[i] * (1.0f - m) + wetR * m) * og;

        if (ng > 1.0e-7f)
        {
            outL += pinkL.next (rng) * ng;
            outR += pinkR.next (rng) * ng;
        }

        l[i] = outL;
        r[i] = outR;
    }
}

//==============================================================================
juce::AudioProcessorEditor* PulseAudioProcessor::createEditor()
{
    return new PulseAudioProcessorEditor (*this);
}

void PulseAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PulseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseAudioProcessor();
}
