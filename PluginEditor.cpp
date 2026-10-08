#include "PluginEditor.h"

//==============================================================================
PulseLookAndFeel::PulseLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       juce::Colours::white.withAlpha (0.85f));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,  accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId,               textDim);
}

void PulseLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    const float lineW = 3.0f;
    const float arcR  = radius - lineW * 0.5f;
    const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackPath;
    trackPath.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (track.withMultipliedAlpha (alpha));
    g.strokePath (trackPath, stroke);

    juce::Path valuePath;
    valuePath.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (valuePath, stroke);

    const float bodyR = radius - 9.0f;
    g.setColour (body.withMultipliedAlpha (alpha));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyR + 3.0f, 3.0f, bodyR * 0.45f, 1.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (juce::Colours::white.withAlpha (0.9f * alpha));
    g.fillPath (pointer);
}

void PulseLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    auto r = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? accent : track);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText (button.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
PulseAudioProcessorEditor::PulseAudioProcessorEditor (PulseAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&laf);

    setupKnob (knobs[0], "DRIVE",     ParamIDs::drive);
    setupKnob (knobs[1], "CHARACTER", ParamIDs::character);
    setupKnob (knobs[2], "DRIFT",     ParamIDs::drift);
    setupKnob (knobs[3], "MIX",       ParamIDs::mix);
    setupKnob (knobs[4], "OUTPUT",    ParamIDs::output);
    setupKnob (knobs[5], "NOISE LVL", ParamIDs::noiseLevel);

    addAndMakeVisible (noiseToggle);
    noiseAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::noiseOn, noiseToggle);
    noiseToggle.setClickingTogglesState (true);
    noiseToggle.onStateChange = [this] { knobs[5].slider.setEnabled (noiseToggle.getToggleState()); };
    knobs[5].slider.setEnabled (noiseToggle.getToggleState());

    setSize (640, 330);
}

PulseAudioProcessorEditor::~PulseAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void PulseAudioProcessorEditor::setupKnob (Knob& knob, const juce::String& name, const juce::String& paramID)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f, true);
    knob.slider.setDoubleClickReturnValue (true, audioProcessor.apvts.getParameter (paramID)->convertFrom0to1 (
                                                     audioProcessor.apvts.getParameter (paramID)->getDefaultValue()));
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramID, knob.slider);
}

//==============================================================================
void PulseAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (PulseLookAndFeel::background);

    auto header = getLocalBounds().removeFromTop (74).reduced (28, 0);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText ("PULSE", header.removeFromLeft (150).withTrimmedTop (14).withHeight (34),
                juce::Justification::centredLeft);

    g.setColour (PulseLookAndFeel::accent);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("HIM'Z DSP", header.withTrimmedTop (28).withHeight (20), juce::Justification::centredLeft);

    g.setColour (PulseLookAndFeel::track);
    g.fillRect (28, 74, getWidth() - 56, 1);
}

void PulseAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (74);
    noiseToggle.setBounds (header.getRight() - 28 - 96, 23, 96, 28);

    area = area.reduced (28, 16);
    const int cellW = area.getWidth() / (int) knobs.size();

    for (auto& k : knobs)
    {
        auto cell = area.removeFromLeft (cellW);
        k.label.setBounds (cell.removeFromTop (22));
        k.slider.setBounds (cell);
    }
}
