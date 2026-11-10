#include "PluginEditor.h"

#include "Parameters.h"
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

CurveEditor::Hit CurveEditor::hitTest (juce::Point<float> pos) const
{
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

void CurveEditor::pushCurve()
{
    processor.setCurve (curve);
    seenVersion = processor.getCurveVersion();
    edited = true;
    repaint();
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    setMouseCursor (hit.target == Target::none ? juce::MouseCursor::NormalCursor
                                               : hit.target == Target::bend ? juce::MouseCursor::UpDownResizeCursor
                                                                            : juce::MouseCursor::DraggingHandCursor);
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTest (e.position);
    edited = false;

    if (drag.target == Target::bend)
        bendAtDragStart = curve[drag.index].curve;
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto plot = plotArea();

    if (drag.target == Target::point)
    {
        const float x = (e.position.x - plot.getX()) / plot.getWidth();
        const float y = (plot.getBottom() - e.position.y) / plot.getHeight();
        curve.movePoint (drag.index, juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y));
        pushCurve();
    }
    else if (drag.target == Target::bend)
    {
        // Dragging up always lifts the middle of the segment, whichever way it slopes.
        const float rising = curve[drag.index + 1].y >= curve[drag.index].y ? 1.0f : -1.0f;
        const float dragUp = (float) -e.getDistanceFromDragStartY() / 90.0f;
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
    const auto hit = hitTest (e.position);

    if (hit.target == Target::point)
    {
        if (! curve.removePoint (hit.index))
            return;
    }
    else if (hit.target == Target::bend)
    {
        curve.setSegmentCurve (hit.index, 0.0f);
    }
    else
    {
        const auto plot = plotArea();
        const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - e.position.y) / plot.getHeight());

        if (curve.addPoint ({ x, y, 0.0f }) < 0)
            return;
    }

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

    const int mode = juce::roundToInt (processor.apvts.getRawParameterValue (psyduck::params::mode)->load());

    if (mode == psyduck::params::modeAudio)
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

    g.setColour (accent.withAlpha (0.08f));
    g.fillPath (fill);
    g.setColour (accent);
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
PsyDuckAudioProcessorEditor::PsyDuckAudioProcessorEditor (PsyDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curveEditor (p)
{
    addAndMakeVisible (curveEditor);

    for (int i = 0; i < psyduck::numPresets; ++i)
        presetBox.addItem (psyduck::presetNames[(size_t) i], i + 1);

    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0)
        {
            processor.setCurve (psyduck::makePreset (id - 1));
            processor.curveEditFinished();
        }
    };
    addAndMakeVisible (presetBox);

    modeBox.addItemList ({ "Sync", "Audio trigger", "MIDI trigger" }, 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, psyduck::params::mode, modeBox);

    hint.setText ("Drag points. Double-click to add or remove a point. Drag the small ring to bend a segment.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, text.withAlpha (0.55f));
    hint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hint);

    rateLabel.setText ("Rate", juce::dontSendNotification);
    rateLabel.setJustificationType (juce::Justification::centred);
    rateLabel.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (rateLabel);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (psyduck::params::rate)))
        rateBox.addItemList (choice->choices, 1);

    addAndMakeVisible (rateBox);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, psyduck::params::rate, rateBox);

    setupKnob (amount, psyduck::params::amount, "Amount");
    setupKnob (mix, psyduck::params::mix, "Mix");
    setupKnob (phase, psyduck::params::phase, "Phase");
    setupKnob (threshold, psyduck::params::threshold, "Threshold");
    setupKnob (retrigger, psyduck::params::retrigger, "Retrigger");
    setupKnob (lookahead, psyduck::params::lookahead, "Lookahead");

    setResizable (true, true);
    setResizeLimits (700, 420, 1600, 1000);
    setSize (820, 520);
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

void PsyDuckAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("PsyDuck", getLocalBounds().removeFromTop (44).reduced (16, 0), juce::Justification::centredLeft);
}

void PsyDuckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (28);
    presetBox.setBounds (header.removeFromRight (190));
    header.removeFromRight (10);
    modeBox.setBounds (header.removeFromRight (140));
    area.removeFromTop (10);

    auto controls = area.removeFromBottom (110);
    area.removeFromBottom (6);
    hint.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);
    curveEditor.setBounds (area);

    const int column = controls.getWidth() / 7;

    auto rateArea = controls.removeFromLeft (column);
    rateLabel.setBounds (rateArea.removeFromTop (20));
    rateBox.setBounds (rateArea.withSizeKeepingCentre (juce::jmin (110, rateArea.getWidth() - 10), 28));

    for (auto* knob : { &amount, &mix, &phase, &threshold, &retrigger, &lookahead })
    {
        auto knobArea = controls.removeFromLeft (column);
        knob->label.setBounds (knobArea.removeFromTop (20));
        knob->slider.setBounds (knobArea);
    }
}
