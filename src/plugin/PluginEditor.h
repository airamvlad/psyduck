#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

/** Interactive ducking curve with the kick and the bass drawn underneath.

    Drag a point to move it, drag the small ring in the middle of a segment to
    bend it, double-click empty space to add a point, double-click a point to
    remove it. Hold Shift to flip grid snapping for one drag.
    Mouse wheel zooms in time, Shift + wheel scrolls. */
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Shows the whole cycle again. */
    void resetZoom();
    bool isZoomed() const noexcept { return viewEnd - viewStart < 0.999f; }

    /** true: kick and bass as waveforms. false: as levels. */
    bool showWaveform = false;

private:
    enum class Target { none, point, bend };

    struct Hit
    {
        Target target = Target::none;
        std::size_t index = 0;
    };

    void timerCallback() override;

    int band() const;
    bool bandsShown() const;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float>, bool snap) const;
    juce::Point<float> bendHandle (std::size_t segment) const;
    bool hasBendHandle (std::size_t segment) const;
    Hit findTarget (juce::Point<float>) const;
    bool snapFor (const juce::MouseEvent&) const;
    void beginEdit();
    void pushCurve();
    void drawScope (juce::Graphics&, juce::Rectangle<float> plot) const;
    juce::Path curvePath (const psyduck::Curve&, juce::Rectangle<float> plot) const;

    PsyDuckAudioProcessor& processor;
    psyduck::Curve curve;
    int seenVersion = -1;
    int seenBand = -1;

    float viewStart = 0.0f, viewEnd = 1.0f;

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

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setupKnob (Knob&, const char* paramId, const juce::String& title, const juce::String& suffix = {});
    void setupButton (juce::TextButton&, const juce::String& title, const juce::String& tooltip = {});
    void setupLabel (juce::Label&, const juce::String& title);
    void setToggleLook (juce::TextButton&);
    void updateLayoutState();
    void rebuildPresetMenu();
    void presetChosen();
    void savePresetDialog();
    void suggestCurve();
    void copyCurve();
    void pasteCurve();
    void alignPhase();
    void showMessage (const juce::String&);

    static juce::File presetFolder();

    PsyDuckAudioProcessor& processor;

    CurveEditor curveEditor;
    PhaseMeterView phaseMeter;

    // Header
    juce::ComboBox modeBox, targetBox, presetBox;
    std::unique_ptr<ComboAttachment> modeAttachment, targetAttachment;
    juce::TextButton slotA, slotB, slotCopy;
    juce::Array<juce::File> userPresets;

    // Toolbar
    std::array<juce::TextButton, 3> bandTabs;
    juce::TextButton undoButton, redoButton, snapButton, suggestButton, copyButton, pasteButton;
    juce::TextButton waveButton, freezeButton, fitButton;
    juce::ComboBox gridBox;

    juce::Label hint;
    int messageTicks = 0;

    // Main controls
    juce::ComboBox rateBox, noteBox;
    juce::Label rateLabel, noteLabel;
    std::unique_ptr<ComboAttachment> rateAttachment, noteAttachment;
    Knob amount, mix, phase, threshold, retrigger, release, lookahead, bassDelay;

    juce::Label phaseLabel;
    juce::ToggleButton invertButton { "Invert" };
    juce::TextButton alignButton;
    std::unique_ptr<ButtonAttachment> invertAttachment, multibandAttachment;

    // Second row: depends on Target
    juce::ToggleButton multibandButton { "Multiband" };
    Knob crossoverLow, crossoverHigh, amountLow, amountMid, amountHigh, filterMin, resonance;
    juce::Label panInfo;

    juce::TooltipWindow tooltips { this };

    int shownMode = -1, shownTarget = -1, shownBand = -1, shownSlot = -1;
    bool shownMultiband = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessorEditor)
};
