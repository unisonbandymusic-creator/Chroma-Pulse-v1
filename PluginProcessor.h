#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace ParamIDs
{
    inline constexpr const char* drive      = "drive";       // 0..30 dB
    inline constexpr const char* character  = "character";   // 0..100 %  (0 = odd, 100 = even-rich)
    inline constexpr const char* drift      = "drift";       // 0..100 %
    inline constexpr const char* mix        = "mix";         // 0..100 %
    inline constexpr const char* output     = "output";      // -24..+12 dB
    inline constexpr const char* noiseOn    = "noiseOn";     // bool
    inline constexpr const char* noiseLevel = "noiseLevel";  // -96..-60 dB
}

//==============================================================================
/** Pulse by Him'z DSP: 4x oversampled hybrid even/odd saturator with analog
    micro-drift (gain + pan) and an optional, scalable noise floor. */
class PulseAudioProcessor : public juce::AudioProcessor
{
public:
    PulseAudioProcessor();
    ~PulseAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    //==============================================================================
    /** Smoothed random-walk used for the slow analog-style drift. Output is in [-1, 1]. */
    struct DriftWalker
    {
        float current = 0.0f, target = 0.0f;
        int samplesLeft = 0;

        float next (juce::Random& rng, float coef, int minLen, int maxLen) noexcept
        {
            if (--samplesLeft <= 0)
            {
                target = rng.nextFloat() * 2.0f - 1.0f;
                samplesLeft = minLen + rng.nextInt (maxLen - minLen + 1);
            }
            current += coef * (target - current);
            return current;
        }
    };

    /** Paul Kellet's economy pink-noise filter. */
    struct PinkNoise
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;

        float next (juce::Random& rng) noexcept
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            return (b0 + b1 + b2 + w * 0.1848f) * 0.11f;
        }
    };

    void processChunk (juce::AudioBuffer<float>& buffer);

    // Parameters (raw, lock-free)
    std::atomic<float>* driveParam      = nullptr;
    std::atomic<float>* characterParam  = nullptr;
    std::atomic<float>* driftParam      = nullptr;
    std::atomic<float>* mixParam        = nullptr;
    std::atomic<float>* outputParam     = nullptr;
    std::atomic<float>* noiseOnParam    = nullptr;
    std::atomic<float>* noiseLevelParam = nullptr;

    // Oversampling (4x, polyphase IIR half-band, integer latency)
    juce::dsp::Oversampling<float> oversampler { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    // Latency-matched dry path for the mix control
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1024 };
    juce::AudioBuffer<float> dryBuffer;

    // Per-sample gain (drift incl. pan) buffers, filled before oversampling
    std::vector<float> gainBufL, gainBufR;

    // Smoothers
    juce::SmoothedValue<float> driveSm, characterSm, mixSm, outputSm, noiseSm, driftSm;

    // Drift + noise generators
    juce::Random rng;
    DriftWalker walkerL, walkerR, walkerPan;
    PinkNoise pinkL, pinkR;
    float driftCoef = 0.0f;
    int driftMinLen = 1, driftMaxLen = 2;

    // DC blocker (runs at oversampled rate, after the saturator)
    float dcR = 0.999f;
    float dcX1[2] { 0.0f, 0.0f };
    float dcY1[2] { 0.0f, 0.0f };

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseAudioProcessor)
};
