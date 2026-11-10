#include "PluginEditor.h"

#include "Parameters.h"
#include "dsp/CurveSuggest.h"
#include "dsp/Presets.h"

namespace
{
const juce::Colour background { 0xff14161c };
const juce::Colour panel { 0xff1d2029 };
const juce::Colour grid { 0xff2c313d };
const juce::Colour accent { 0xff7dffb0 };
const juce::Colour text { 0xffd7dbe6 };
const juce::Colour kickColour { 0xffff9a4d };
const juce::Colour bassColour { 0xff5aa9ff };

constexpr float pointRadius = 6.0f;
constexpr float bendRadius = 4.0f;
constexpr float grabRadius = 11.0f;
constexpr int snapColumns = 16;
constexpr int snapRows = 8;
const juce::Colour good { 0xff7dffb0 };
const juce::Colour bad { 0xffff5a6a };

int currentMode (PsyDuckAudioProcessor& p)
{
    return juce::roundToInt (p.apvts.getRawParameterValue (psyduck::params::mode)->load());
}
} // namespace

//==============================================================================
CurveEditor::CurveEditor (PsyDuckAudioProcessor& p) : processor (p)
{
    curve = processor.getCurve();
    seenVersion = processor.getCurveVersion();
    startTimerHz (30);
}

void CurveEditor::timerCallback()
{
    const int version = processor.getCurveVersion();

    if (version != seenVersion && drag.target == Target::none)
    {
        curve = processor.getCurve();
        seenVersion = version;
    }

    repaint();
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto plot = plotArea();
    return { plot.getX() + plot.getWidth() * x, plot.getBottom() - plot.getHeight() * y };
}

juce::Point<float> CurveEditor::fromScreen (juce::Point<float> pos, bool snap) const
{
    const auto plot = plotArea();
    float x = juce::jlimit (0.0f, 1.0f, (pos.x - plot.getX()) / plot.getWidth());
    float y = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - pos.y) / plot.getHeight());

    if (snap)
    {
        x = std::round (x * (float) snapColumns) / (float) snapColumns;
        y = std::round (y * (float) snapRows) / (float) snapRows;
    }

    return { x, y };
}

bool CurveEditor::snapFor (const juce::MouseEvent& e) const
{
    // Shift flips the Snap setting for one gesture.
    return processor.snapToGrid.load() != e.mods.isShiftDown();
}

bool CurveEditor::hasBendHandle (std::size_t segment) const
{
    if (segment + 1 >= curve.size())
        return false;

    const auto a = toScreen (curve[segment].x, curve[segment].y);
    const auto b = toScreen (curve[segment + 1].x, curve[segment + 1].y);

    // A flat or very short segment has nothing to bend.
    return (b.x - a.x) > 30.0f && std::abs (b.y - a.y) > 8.0f;
}

juce::Point<float> CurveEditor::bendHandle (std::size_t segment) const
{
    const float midX = 0.5f * (curve[segment].x + curve[segment + 1].x);
    return toScreen (midX, curve.evaluate (midX));
}

CurveEditor::Hit CurveEditor::findTarget (juce::Point<float> pos) const
{
    if (currentMode (processor) == psyduck::params::modeFollower)
        return {};

    Hit best;
    float bestDistance = grabRadius;

    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const float d = pos.getDistanceFrom (toScreen (curve[i].x, curve[i].y));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = { Target::point, i };
        }
    }

    if (best.target == Target::point)
        return best;

    for (std::size_t i = 0; i + 1 < curve.size(); ++i)
    {
        if (! hasBendHandle (i))
            continue;

        const float d = pos.getDistanceFrom (bendHandle (i));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = { Target::bend, i };
        }
    }

    return best;
}

void CurveEditor::beginEdit()
{
    // One undo step per gesture, taken just before the first change.
    if (! edited)
        processor.beginCurveEdit();
}

void CurveEditor::pushCurve()
{
    processor.setCurve (curve);
    seenVersion = processor.getCurveVersion();
    edited = true;
    repaint();
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = findTarget (e.position);
    setMouseCursor (hit.target == Target::none ? juce::MouseCursor::NormalCursor
                                               : hit.target == Target::bend ? juce::MouseCursor::UpDownResizeCursor
                                                                            : juce::MouseCursor::DraggingHandCursor);
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    drag = findTarget (e.position);
    edited = false;

    if (drag.target == Target::bend)
        bendAtDragStart = curve[drag.index].curve;
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.target == Target::point)
    {
        const auto p = fromScreen (e.position, snapFor (e));
        beginEdit();
        curve.movePoint (drag.index, p.x, p.y);
        pushCurve();
    }
    else if (drag.target == Target::bend)
    {
        // Dragging up always lifts the middle of the segment, whichever way it slopes.
        const float rising = curve[drag.index + 1].y >= curve[drag.index].y ? 1.0f : -1.0f;
        const float dragUp = (float) -e.getDistanceFromDragStartY() / 90.0f;
        beginEdit();
        curve.setSegmentCurve (drag.index, bendAtDragStart + rising * dragUp);
        pushCurve();
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    drag = {};

    if (edited)
        processor.curveEditFinished();

    edited = false;
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (currentMode (processor) == psyduck::params::modeFollower)
        return;

    const auto hit = findTarget (e.position);
    auto changed = curve;

    if (hit.target == Target::point)
    {
        if (! changed.removePoint (hit.index))
            return;
    }
    else if (hit.target == Target::bend)
    {
        changed.setSegmentCurve (hit.index, 0.0f);
    }
    else
    {
        const auto p = fromScreen (e.position, snapFor (e));
        if (changed.addPoint ({ p.x, p.y, 0.0f }) < 0)
            return;
    }

    edited = false;
    beginEdit();
    curve = changed;
    pushCurve();
    processor.curveEditFinished();
    edited = false;
}

void CurveEditor::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    const auto plot = plotArea();

    g.setColour (grid);
    for (int i = 0; i <= 4; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    if (processor.snapToGrid.load())
    {
        g.setColour (grid.withAlpha (0.45f));
        for (int i = 1; i < snapColumns; ++i)
            if (i % 4 != 0)
                g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) i / (float) snapColumns), plot.getY(), plot.getBottom());
        for (int i = 1; i < snapRows; ++i)
            if (i % 2 != 0)
                g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) i / (float) snapRows), plot.getX(), plot.getRight());
    }

    // Kick (sidechain) and bass (output) levels along the cycle.
    const auto& scope = processor.getScope();
    const auto drawLevels = [&] (bool kick, juce::Colour colour)
    {
        juce::Path shape;
        shape.startNewSubPath (plot.getX(), plot.getBottom());

        for (int bin = 0; bin < psyduck::Scope::numBins; ++bin)
        {
            const float level = juce::jlimit (0.0f, 1.0f, kick ? scope.getKick (bin) : scope.getBass (bin));
            const float x0 = plot.getX() + plot.getWidth() * (float) bin / (float) psyduck::Scope::numBins;
            const float x1 = plot.getX() + plot.getWidth() * (float) (bin + 1) / (float) psyduck::Scope::numBins;
            const float y = plot.getBottom() - plot.getHeight() * level;
            shape.lineTo (x0, y);
            shape.lineTo (x1, y);
        }

        shape.lineTo (plot.getRight(), plot.getBottom());
        shape.closeSubPath();
        g.setColour (colour.withAlpha (0.30f));
        g.fillPath (shape);
    };

    drawLevels (false, bassColour);
    drawLevels (true, kickColour);

    const int mode = currentMode (processor);
    const bool curveUsed = mode != psyduck::params::modeFollower;

    if (mode == psyduck::params::modeAudio || mode == psyduck::params::modeFollower)
    {
        const float thresholdGain = juce::Decibels::decibelsToGain (
            processor.apvts.getRawParameterValue (psyduck::params::threshold)->load());
        const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, thresholdGain);
        g.setColour (kickColour.withAlpha (0.8f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    const int steps = juce::jmax (2, juce::roundToInt (plot.getWidth()));

    juce::Path line;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const auto p = toScreen (t, curve.evaluate (t));

        if (i == 0)
            line.startNewSubPath (p);
        else
            line.lineTo (p);
    }

    juce::Path fill (line);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    g.setColour (accent.withAlpha (curveUsed ? 0.08f : 0.02f));
    g.fillPath (fill);
    g.setColour (accent.withAlpha (curveUsed ? 1.0f : 0.25f));
    g.strokePath (line, juce::PathStrokeType (2.0f));

    const float playhead = processor.getDisplayPhase();
    if (playhead >= 0.0f)
    {
        const float playheadX = plot.getX() + plot.getWidth() * playhead;
        g.setColour (text.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (playheadX), plot.getY(), plot.getBottom());
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kickColour);
    g.drawText ("KICK (sidechain)", plot.reduced (6.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topRight);
    g.setColour (bassColour);
    g.drawText ("BASS (output)", plot.reduced (6.0f, 4.0f).translated (0.0f, 14.0f).removeFromTop (14.0f), juce::Justification::topRight);

    if (! curveUsed)
    {
        g.setColour (text.withAlpha (0.7f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Follower mode: the bass follows the kick level, the curve is not used",
                    plot, juce::Justification::centred);
        return;
    }

    for (std::size_t i = 0; i + 1 < curve.size(); ++i)
    {
        if (! hasBendHandle (i))
            continue;

        const auto p = bendHandle (i);
        g.setColour (background);
        g.fillEllipse (juce::Rectangle<float> (bendRadius * 2.0f, bendRadius * 2.0f).withCentre (p));
        g.setColour (accent.withAlpha (0.8f));
        g.drawEllipse (juce::Rectangle<float> (bendRadius * 2.0f, bendRadius * 2.0f).withCentre (p), 1.5f);
    }

    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const auto p = toScreen (curve[i].x, curve[i].y);
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (p));
        g.setColour (background);
        g.drawEllipse (juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (p), 1.5f);
    }
}

//==============================================================================
PhaseMeterView::PhaseMeterView (PsyDuckAudioProcessor& p) : processor (p)
{
    startTimerHz (20);
}

void PhaseMeterView::timerCallback()
{
    valid = processor.hasPhaseReading();
    const float target = valid ? processor.getPhaseCorrelation() : 0.0f;
    shown += (target - shown) * 0.3f;
    repaint();
}

void PhaseMeterView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto textArea = area.removeFromBottom (18.0f);
    auto bar = area.withSizeKeepingCentre (area.getWidth() - 8.0f, 12.0f);

    g.setColour (panel);
    g.fillRoundedRectangle (bar, 3.0f);

    const float centre = bar.getCentreX();
    g.setColour (text.withAlpha (0.4f));
    g.drawVerticalLine (juce::roundToInt (centre), bar.getY() - 3.0f, bar.getBottom() + 3.0f);

    juce::String status = "no kick + bass";
    juce::Colour colour = text.withAlpha (0.5f);

    if (valid)
    {
        if (shown > 0.3f)
        {
            status = "in phase";
            colour = good;
        }
        else if (shown < -0.3f)
        {
            status = "cancelling";
            colour = bad;
        }
        else
        {
            status = "neutral";
            colour = text;
        }

        const float end = centre + shown * bar.getWidth() * 0.5f;
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (centre, end), bar.getY(),
                                                         std::abs (end - centre), bar.getHeight()), 2.0f);
    }

    g.setColour (colour);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (status, textArea, juce::Justification::centred);
}

//==============================================================================
PsyDuckAudioProcessorEditor::PsyDuckAudioProcessorEditor (PsyDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curveEditor (p), phaseMeter (p)
{
    addAndMakeVisible (curveEditor);

    setupHeaderButton (undoButton, "Undo");
    setupHeaderButton (redoButton, "Redo");
    setupHeaderButton (snapButton, "Snap");
    setupHeaderButton (suggestButton, "Suggest");

    undoButton.onClick = [this] { processor.undoCurve(); };
    redoButton.onClick = [this] { processor.redoCurve(); };
    suggestButton.onClick = [this] { suggestCurve(); };
    suggestButton.setTooltip ("Builds a starting curve from the kick shown under the curve");

    snapButton.setClickingTogglesState (true);
    snapButton.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
    snapButton.setToggleState (processor.snapToGrid.load(), juce::dontSendNotification);
    snapButton.onClick = [this]
    {
        processor.snapToGrid.store (snapButton.getToggleState());
        processor.curveEditFinished();
    };

    modeBox.addItemList ({ "Sync", "Audio trigger", "MIDI trigger", "Follower" }, 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, psyduck::params::mode, modeBox);

    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    rebuildPresetMenu();

    hint.setFont (juce::FontOptions (12.0f));
    hint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hint);
    showMessage ({});

    rateLabel.setText ("Rate", juce::dontSendNotification);
    rateLabel.setJustificationType (juce::Justification::centred);
    rateLabel.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (rateLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (psyduck::params::rate)))
        rateBox.addItemList (choice->choices, 1);

    addAndMakeVisible (rateBox);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, psyduck::params::rate, rateBox);

    noteLabel.setText ("Note", juce::dontSendNotification);
    noteLabel.setJustificationType (juce::Justification::centred);
    noteLabel.setColour (juce::Label::textColourId, text);
    addChildComponent (noteLabel);

    noteBox.addItem ("Any", 1);
    for (int note = 0; note < 128; ++note)
        noteBox.addItem (juce::MidiMessage::getMidiNoteName (note, true, true, 3), note + 2);
    addChildComponent (noteBox);
    noteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, psyduck::params::midiNote, noteBox);

    setupKnob (amount, psyduck::params::amount, "Amount");
    setupKnob (mix, psyduck::params::mix, "Mix");
    setupKnob (phase, psyduck::params::phase, "Phase");
    setupKnob (threshold, psyduck::params::threshold, "Threshold");
    setupKnob (retrigger, psyduck::params::retrigger, "Retrigger");
    setupKnob (release, psyduck::params::release, "Release");
    setupKnob (lookahead, psyduck::params::lookahead, "Lookahead");
    setupKnob (crossover, psyduck::params::crossover, "Crossover");
    crossover.label.setVisible (false);

    for (auto* button : { &multibandButton, &invertButton })
    {
        button->setColour (juce::ToggleButton::textColourId, text);
        button->setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (*button);
    }

    multibandButton.setTooltip ("Duck only the low end below the crossover frequency");
    invertButton.setTooltip ("Flip the polarity of the bass. Try it when the meter says cancelling");

    multibandAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, psyduck::params::multiband, multibandButton);
    invertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, psyduck::params::invert, invertButton);

    phaseLabel.setText ("Kick / Bass phase", juce::dontSendNotification);
    phaseLabel.setJustificationType (juce::Justification::centred);
    phaseLabel.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (phaseLabel);
    addAndMakeVisible (phaseMeter);

    updateForMode();
    startTimerHz (10);

    setResizable (true, true);
    setResizeLimits (900, 460, 1800, 1100);
    setSize (980, 560);
}

void PsyDuckAudioProcessorEditor::setupHeaderButton (juce::TextButton& button, const juce::String& title)
{
    button.setButtonText (title);
    button.setColour (juce::TextButton::buttonColourId, panel);
    button.setColour (juce::TextButton::textColourOffId, text);
    button.setColour (juce::TextButton::textColourOnId, text);
    addAndMakeVisible (button);
}

void PsyDuckAudioProcessorEditor::setupKnob (Knob& knob, const char* paramId, const juce::String& title)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setColour (juce::Slider::thumbColourId, accent);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, text);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob.slider);

    knob.label.setText (title, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramId, knob.slider);
}

void PsyDuckAudioProcessorEditor::timerCallback()
{
    const int mode = currentMode (processor);
    const bool multiband = multibandButton.getToggleState();

    if (mode != shownMode || multiband != shownMultiband)
        updateForMode();

    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());

    if (snapButton.getToggleState() != processor.snapToGrid.load())
        snapButton.setToggleState (processor.snapToGrid.load(), juce::dontSendNotification);

    if (messageTicks > 0 && --messageTicks == 0)
        showMessage ({});
}

void PsyDuckAudioProcessorEditor::updateForMode()
{
    using namespace psyduck::params;

    shownMode = currentMode (processor);
    shownMultiband = multibandButton.getToggleState();

    const bool sync = shownMode == modeSync;
    const bool audio = shownMode == modeAudio;
    const bool midi = shownMode == modeMidi;
    const bool follow = shownMode == modeFollower;

    // Only the controls that do something in this mode stay lit.
    phase.setActive (sync);
    threshold.setActive (audio || follow);
    lookahead.setActive (true);
    crossover.setActive (shownMultiband);

    // One slot, three uses: Retrigger (Audio), Note (MIDI), Release (Follower).
    retrigger.setShown (sync || audio);
    retrigger.setActive (audio);
    release.setShown (follow);
    noteBox.setVisible (midi);
    noteLabel.setVisible (midi);

    rateBox.setEnabled (! follow);
    rateBox.setAlpha (follow ? 0.3f : 1.0f);
    suggestButton.setEnabled (! follow);
    presetBox.setEnabled (! follow);

    if (messageTicks == 0)
        showMessage ({});
}

void PsyDuckAudioProcessorEditor::showMessage (const juce::String& message)
{
    if (message.isNotEmpty())
    {
        hint.setText (message, juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, kickColour);
        messageTicks = 40;
        return;
    }

    messageTicks = 0;
    hint.setColour (juce::Label::textColourId, text.withAlpha (0.55f));

    switch (currentMode (processor))
    {
        case psyduck::params::modeAudio:
            hint.setText ("Audio trigger: route the kick to the sidechain input. Threshold (orange line) sets how loud it must be.",
                          juce::dontSendNotification);
            break;
        case psyduck::params::modeMidi:
            hint.setText ("MIDI trigger: every note (or only the chosen Note) starts the curve.", juce::dontSendNotification);
            break;
        case psyduck::params::modeFollower:
            hint.setText ("Follower: a kick at the Threshold level ducks fully. Release sets how fast the bass comes back.",
                          juce::dontSendNotification);
            break;
        default:
            hint.setText ("Drag points. Double-click to add or remove a point. Drag the small ring to bend. Shift flips Snap.",
                          juce::dontSendNotification);
            break;
    }
}

//==============================================================================
juce::File PsyDuckAudioProcessorEditor::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("PsyDuck")
        .getChildFile ("Presets");
}

void PsyDuckAudioProcessorEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);

    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < psyduck::numPresets; ++i)
        presetBox.addItem (psyduck::presetNames[(size_t) i], 1 + i);

    userPresets = presetFolder().findChildFiles (juce::File::findFiles, false, "*.psycurve");
    userPresets.sort();

    if (! userPresets.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 100 + i);
    }

    presetBox.addSeparator();
    presetBox.addItem ("Save current curve...", 900);
    presetBox.addItem ("Open preset folder", 901);
}

void PsyDuckAudioProcessorEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();

    if (id == 900)
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        savePresetDialog();
        return;
    }

    if (id == 901)
    {
        presetBox.setSelectedId (0, juce::dontSendNotification);
        presetFolder().createDirectory();
        presetFolder().revealToUser();
        return;
    }

    psyduck::Curve loaded;

    if (id >= 100 && id - 100 < userPresets.size())
    {
        if (! PsyDuckAudioProcessor::curveFromString (userPresets[id - 100].loadFileAsString().trim(), loaded))
        {
            showMessage ("That preset file could not be read.");
            return;
        }
    }
    else if (id >= 1 && id <= psyduck::numPresets)
    {
        loaded = psyduck::makePreset (id - 1);
    }
    else
    {
        return;
    }

    processor.beginCurveEdit();
    processor.setCurve (loaded);
    processor.curveEditFinished();
}

void PsyDuckAudioProcessorEditor::savePresetDialog()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name for this curve:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", "My curve");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PsyDuckAudioProcessorEditor> safeThis (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());
        if (name.isEmpty())
            return;

        const auto folder = presetFolder();
        const auto file = folder.getChildFile (name + ".psycurve");

        if (! folder.createDirectory().wasOk()
            || ! file.replaceWithText (PsyDuckAudioProcessor::curveToString (safeThis->processor.getCurve())))
        {
            safeThis->showMessage ("Could not save the preset to " + folder.getFullPathName());
            return;
        }

        safeThis->rebuildPresetMenu();
        safeThis->showMessage ("Saved preset \"" + name + "\".");
    }), true);
}

void PsyDuckAudioProcessorEditor::suggestCurve()
{
    const auto& scope = processor.getScope();

    std::vector<float> kick ((size_t) psyduck::Scope::numBins);
    for (int i = 0; i < psyduck::Scope::numBins; ++i)
        kick[(size_t) i] = scope.getKick (i);

    psyduck::Curve suggestion;

    if (! psyduck::suggestCurve (kick, suggestion))
    {
        showMessage ("No kick seen yet. Route the kick to the sidechain input and let it play, then press Suggest.");
        return;
    }

    processor.beginCurveEdit();
    processor.setCurve (suggestion);
    processor.curveEditFinished();
    showMessage ("Curve built from the kick. Adjust the points to taste, or press Undo.");
}

//==============================================================================
void PsyDuckAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("PsyDuck", getLocalBounds().removeFromTop (60).reduced (16, 0).removeFromLeft (100),
                juce::Justification::centredLeft);
}

void PsyDuckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (28);
    header.removeFromLeft (104);

    for (auto* button : { &undoButton, &redoButton, &snapButton, &suggestButton })
    {
        button->setBounds (header.removeFromLeft (button == &suggestButton ? 76 : 58));
        header.removeFromLeft (6);
    }

    presetBox.setBounds (header.removeFromRight (190));
    header.removeFromRight (10);
    modeBox.setBounds (header.removeFromRight (140));
    area.removeFromTop (10);

    auto controls = area.removeFromBottom (110);
    area.removeFromBottom (6);
    hint.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);
    curveEditor.setBounds (area);

    const int column = controls.getWidth() / 9;
    const auto next = [&] { return controls.removeFromLeft (column); };

    const auto placeBox = [] (juce::Rectangle<int> slot, juce::Label& label, juce::ComboBox& box)
    {
        label.setBounds (slot.removeFromTop (20));
        box.setBounds (slot.withSizeKeepingCentre (juce::jmin (100, slot.getWidth() - 10), 28));
    };

    const auto placeKnob = [] (juce::Rectangle<int> slot, Knob& knob)
    {
        knob.label.setBounds (slot.removeFromTop (20));
        knob.slider.setBounds (slot);
    };

    placeBox (next(), rateLabel, rateBox);
    placeKnob (next(), amount);
    placeKnob (next(), mix);
    placeKnob (next(), phase);
    placeKnob (next(), threshold);

    const auto shared = next();
    placeKnob (shared, retrigger);
    placeKnob (shared, release);
    placeBox (shared, noteLabel, noteBox);

    placeKnob (next(), lookahead);

    auto bandSlot = next();
    multibandButton.setBounds (bandSlot.removeFromTop (20).reduced (2, 0));
    crossover.slider.setBounds (bandSlot);

    auto phaseSlot = controls;
    phaseLabel.setBounds (phaseSlot.removeFromTop (20));
    invertButton.setBounds (phaseSlot.removeFromBottom (24).withSizeKeepingCentre (76, 22));
    phaseMeter.setBounds (phaseSlot.reduced (2, 6));
}
