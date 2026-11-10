#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

/** Interactive ducking curve.
    Drag a point to move it, drag the small handle in the middle of a segment
    to bend it, double-click empty space to add a point, double-click a point
    to remove it. */
class CurveEditor final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CurveEditor (PsyDuckAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Target { none, point, bend };

    struct Hit
    {
        Target target = Target::none;
        std::size_t index = 0;
    };

    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> bendHandle (std::size_t segment) const;
    bool hasBendHandle (std::size_t segment) const;
    Hit hitTest (juce::Point<float>) const;
    void pushCurve();

    PsyDuckAudioProcessor& processor;
    psyduck::Curve curve;
    int seenVersion = -1;

    Hit drag;
    float bendAtDragStart = 0.0f;
    bool edited = false;
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

    CurveEditor curveEditor;
    juce::ComboBox presetBox;
    juce::Label hint;
    juce::ComboBox rateBox;
    juce::Label rateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;
    Knob amount, mix, phase;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessorEditor)
};
