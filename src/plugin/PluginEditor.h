#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

/** Interactive ducking curve with the kick and bass levels drawn underneath.
    Drag a point to move it, drag the small ring in the middle of a segment
    to bend it, double-click empty space to add a point, double-click a point
    to remove it. Hold Shift to switch grid snapping for one drag. */
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
    juce::Point<float> fromScreen (juce::Point<float>, bool snap) const;
    juce::Point<float> bendHandle (std::size_t segment) const;
    bool hasBendHandle (std::size_t segment) const;
    Hit findTarget (juce::Point<float>) const;
    bool snapFor (const juce::MouseEvent&) const;
    void beginEdit();
    void pushCurve();

    PsyDuckAudioProcessor& processor;
    psyduck::Curve curve;
    int seenVersion = -1;

    Hit drag;
    float bendAtDragStart = 0.0f;
    bool edited = false;
};

/** Shows whether the low end of kick and bass adds up or cancels. */
class PhaseMeterView final : public juce::Component,
                             private juce::Timer
{
public:
    explicit PhaseMeterView (PsyDuckAudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PsyDuckAudioProcessor& processor;
    float shown = 0.0f;
    bool valid = false;
};

class PsyDuckAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
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

        void setActive (bool active)
        {
            slider.setEnabled (active);
            slider.setAlpha (active ? 1.0f : 0.3f);
            label.setAlpha (active ? 1.0f : 0.3f);
        }

        void setShown (bool shown)
        {
            slider.setVisible (shown);
            label.setVisible (shown);
        }
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* paramId, const juce::String& title);
    void setupHeaderButton (juce::TextButton&, const juce::String& title);
    void updateForMode();
    void rebuildPresetMenu();
    void presetChosen();
    void savePresetDialog();
    void suggestCurve();
    void showMessage (const juce::String&);

    static juce::File presetFolder();

    PsyDuckAudioProcessor& processor;

    CurveEditor curveEditor;
    PhaseMeterView phaseMeter;

    juce::TextButton undoButton, redoButton, snapButton, suggestButton;
    juce::ComboBox modeBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    juce::Array<juce::File> userPresets;

    juce::Label hint;
    int messageTicks = 0;

    juce::ComboBox rateBox;
    juce::Label rateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;

    juce::ComboBox noteBox;
    juce::Label noteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noteAttachment;

    Knob amount, mix, phase, threshold, retrigger, release, lookahead, crossover;

    juce::ToggleButton multibandButton { "Multiband" };
    juce::ToggleButton invertButton { "Invert" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> multibandAttachment, invertAttachment;
    juce::Label phaseLabel;

    juce::TooltipWindow tooltips { this };

    int shownMode = -1;
    bool shownMultiband = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessorEditor)
};
