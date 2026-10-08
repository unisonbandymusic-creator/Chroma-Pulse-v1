#pragma once

#include "PluginProcessor.h"
#include <array>

//==============================================================================
/** Flat, minimalist look: thin arc, dark knob body, single accent colour. */
class PulseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour accent     { 0xffff4d5e };
    static inline const juce::Colour background { 0xff14171c };
    static inline const juce::Colour track      { 0xff2a2f38 };
    static inline const juce::Colour body       { 0xff1e222a };
    static inline const juce::Colour textDim    { 0xff8b93a1 };

    PulseLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
class PulseAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PulseAudioProcessorEditor (PulseAudioProcessor&);
    ~PulseAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob& knob, const juce::String& name, const juce::String& paramID);

    PulseAudioProcessor& audioProcessor;
    PulseLookAndFeel laf;   // declared before the components that use it

    std::array<Knob, 6> knobs;   // Drive, Character, Drift, Mix, Output, Noise Level
    juce::ToggleButton noiseToggle { "NOISE" };
    std::unique_ptr<ButtonAttachment> noiseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseAudioProcessorEditor)
};
