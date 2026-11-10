#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

/** Draws the ducking curve and a playhead that follows the cycle. */
class CurveDisplay final : public juce::Component,
                           private juce::Timer
{
public:
    explicit CurveDisplay (PsyDuckAudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    PsyDuckAudioProcessor& processor;
};

class PsyDuckAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PsyDuckAudioProcessorEditor (PsyDuckAudioProcessor&);
    ~PsyDuckAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramId, const juce::String& title);

    PsyDuckAudioProcessor& processor;

    CurveDisplay curveDisplay;
    juce::ComboBox rateBox;
    juce::Label rateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;
    Knob amount, mix, phase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessorEditor)
};
