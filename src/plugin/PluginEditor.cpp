#include "PluginEditor.h"

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
const juce::Colour good { 0xff7dffb0 };
const juce::Colour bad { 0xffff5a6a };

// Low, mid, high.
const juce::Colour bandColours[] = { juce::Colour (0xff7dffb0), juce::Colour (0xffffd166), juce::Colour (0xffff7ac8) };
const char* const bandNames[] = { "Low", "Mid", "High" };

constexpr float pointRadius = 6.0f;
constexpr float bendRadius = 4.0f;
constexpr float grabRadius = 11.0f;
constexpr int snapRows = 8;
const char* const clipboardPrefix = "PSYDUCK-CURVE:";

float param (PsyDuckAudioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

int currentMode (PsyDuckAudioProcessor& p) { return juce::roundToInt (param (p, psyduck::params::mode)); }
int currentTarget (PsyDuckAudioProcessor& p) { return juce::roundToInt (param (p, psyduck::params::target)); }

/** True when the three bands are in use (Multiband on and the curve drives volume). */
bool bandsActive (PsyDuckAudioProcessor& p)
{
    return param (p, psyduck::params::multiband) > 0.5f && currentTarget (p) == psyduck::params::targetVolume;
}
} // namespace

//==============================================================================
CurveEditor::CurveEditor (PsyDuckAudioProcessor& p) : processor (p)
{
    seenBand = band();
    curve = processor.getCurve (seenBand);
    seenVersion = processor.getCurveVersion();
    startTimerHz (30);
}

int CurveEditor::band() const
{
    return bandsShown() ? juce::jlimit (0, PsyDuckAudioProcessor::numBands - 1, processor.editBand.load()) : 0;
}

bool CurveEditor::bandsShown() const
{
    return bandsActive (processor);
}

void CurveEditor::timerCallback()
{
    const int version = processor.getCurveVersion();
    const int currentBand = band();

    if ((version != seenVersion || currentBand != seenBand) && drag.target == Target::none)
    {
        curve = processor.getCurve (currentBand);
        seenVersion = version;
        seenBand = currentBand;
    }

    repaint();
}

void CurveEditor::resetZoom()
{
    viewStart = 0.0f;
    viewEnd = 1.0f;
    repaint();
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto plot = plotArea();
    const float t = (x - viewStart) / (viewEnd - viewStart);
    return { plot.getX() + plot.getWidth() * t, plot.getBottom() - plot.getHeight() * y };
}

juce::Point<float> CurveEditor::fromScreen (juce::Point<float> pos, bool snap) const
{
    const auto plot = plotArea();
    const float t = (pos.x - plot.getX()) / plot.getWidth();
    float x = juce::jlimit (0.0f, 1.0f, viewStart + t * (viewEnd - viewStart));
    float y = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - pos.y) / plot.getHeight());

    if (snap)
    {
        const auto columns = (float) processor.gridColumns.load();
        x = std::round (x * columns) / columns;
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
    if (currentMode (processor) == psyduck::params::modeFollower || ! plotArea().expanded (grabRadius).contains (pos))
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
        processor.beginCurveEdit (seenBand);
}

void CurveEditor::pushCurve()
{
    processor.setCurve (curve, seenBand);
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
        if (! plotArea().contains (e.position))
            return;

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

void CurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto plot = plotArea();
    const float width = viewEnd - viewStart;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        // Scroll sideways.
        const float amount = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : -wheel.deltaY) * width;
        const float start = juce::jlimit (0.0f, 1.0f - width, viewStart + amount);
        viewStart = start;
        viewEnd = start + width;
    }
    else
    {
        // Zoom around the mouse position, down to 1/64 of the cycle.
        const float anchor = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
        const float centre = viewStart + anchor * width;
        const float newWidth = juce::jlimit (1.0f / 64.0f, 1.0f, width * std::pow (0.5f, wheel.deltaY * 2.0f));
        const float start = juce::jlimit (0.0f, 1.0f - newWidth, centre - anchor * newWidth);
        viewStart = start;
        viewEnd = start + newWidth;
    }

    repaint();
}

juce::Path CurveEditor::curvePath (const psyduck::Curve& c, juce::Rectangle<float> plot) const
{
    const int steps = juce::jmax (2, juce::roundToInt (plot.getWidth()));
    juce::Path line;

    for (int i = 0; i <= steps; ++i)
    {
        const float x = viewStart + (viewEnd - viewStart) * (float) i / (float) steps;
        const auto p = toScreen (x, c.evaluate (x));

        if (i == 0)
            line.startNewSubPath (p);
        else
            line.lineTo (p);
    }

    return line;
}

void CurveEditor::drawScope (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const auto& scope = processor.getScope();
    const int bins = psyduck::Scope::numBins;
    const int first = juce::jlimit (0, bins - 1, (int) std::floor (viewStart * (float) bins));
    const int last = juce::jlimit (first, bins - 1, (int) std::ceil (viewEnd * (float) bins) - 1);

    const auto xOf = [&] (int bin) { return toScreen ((float) bin / (float) bins, 0.0f).x; };

    const auto drawOne = [&] (bool kick, juce::Colour colour)
    {
        juce::Path top, shape;

        if (showWaveform)
        {
            // Waveform: centre line in the middle of the plot.
            const float mid = plot.getCentreY();
            const float half = plot.getHeight() * 0.5f;

            for (int bin = first; bin <= last; ++bin)
            {
                const float hi = juce::jlimit (-1.0f, 1.0f, kick ? scope.getKickMax (bin) : scope.getBassMax (bin));
                const float y = mid - half * hi;
                if (bin == first) shape.startNewSubPath (xOf (bin), y); else shape.lineTo (xOf (bin), y);
                shape.lineTo (xOf (bin + 1), y);
            }

            for (int bin = last; bin >= first; --bin)
            {
                const float lo = juce::jlimit (-1.0f, 1.0f, kick ? scope.getKickMin (bin) : scope.getBassMin (bin));
                const float y = mid - half * lo;
                shape.lineTo (xOf (bin + 1), y);
                shape.lineTo (xOf (bin), y);
            }

            shape.closeSubPath();
            g.setColour (colour.withAlpha (0.45f));
            g.fillPath (shape);
            g.setColour (colour.withAlpha (0.8f));
            g.strokePath (shape, juce::PathStrokeType (1.0f));
            return;
        }

        shape.startNewSubPath (xOf (first), plot.getBottom());
        for (int bin = first; bin <= last; ++bin)
        {
            const float level = juce::jlimit (0.0f, 1.0f, kick ? scope.getKick (bin) : scope.getBass (bin));
            const float y = plot.getBottom() - plot.getHeight() * level;
            shape.lineTo (xOf (bin), y);
            shape.lineTo (xOf (bin + 1), y);
        }
        shape.lineTo (xOf (last + 1), plot.getBottom());
        shape.closeSubPath();
        g.setColour (colour.withAlpha (0.30f));
        g.fillPath (shape);
    };

    drawOne (false, bassColour);
    drawOne (true, kickColour);

    if (showWaveform)
    {
        g.setColour (text.withAlpha (0.25f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    }
}

void CurveEditor::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    const auto plot = plotArea();
    const int mode = currentMode (processor);
    const bool curveUsed = mode != psyduck::params::modeFollower;
    const bool multi = bandsShown();
    const auto curveColour = multi ? bandColours[seenBand < 0 ? 0 : seenBand] : accent;

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt().expanded (1));

        // Grid: every column faintly when Snap is on, quarters always.
        const int columns = processor.gridColumns.load();
        const bool snap = processor.snapToGrid.load();

        for (int i = 0; i <= columns; ++i)
        {
            const bool major = (i * 4) % columns == 0;
            if (! major && ! snap)
                continue;

            const float x = toScreen ((float) i / (float) columns, 0.0f).x;
            if (x < plot.getX() - 1.0f || x > plot.getRight() + 1.0f)
                continue;

            g.setColour (major ? grid : grid.withAlpha (0.45f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }

        for (int i = 0; i <= snapRows; ++i)
        {
            const bool major = i % 2 == 0;
            if (! major && ! snap)
                continue;

            g.setColour (major ? grid : grid.withAlpha (0.45f));
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + plot.getHeight() * (float) i / (float) snapRows), plot.getX(), plot.getRight());
        }

        drawScope (g, plot);

        if (! showWaveform && (mode == psyduck::params::modeAudio || mode == psyduck::params::modeFollower))
        {
            const float thresholdGain = juce::Decibels::decibelsToGain (param (processor, psyduck::params::threshold));
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, thresholdGain);
            g.setColour (kickColour.withAlpha (0.8f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        // The other bands' curves, faintly, for reference.
        if (multi && curveUsed)
        {
            for (int b = 0; b < PsyDuckAudioProcessor::numBands; ++b)
            {
                if (b == seenBand)
                    continue;

                g.setColour (bandColours[b].withAlpha (0.35f));
                g.strokePath (curvePath (processor.getCurve (b), plot), juce::PathStrokeType (1.2f));
            }
        }

        const auto line = curvePath (curve, plot);
        juce::Path fill (line);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();

        g.setColour (curveColour.withAlpha (curveUsed ? 0.08f : 0.02f));
        g.fillPath (fill);
        g.setColour (curveColour.withAlpha (curveUsed ? 1.0f : 0.25f));
        g.strokePath (line, juce::PathStrokeType (2.0f));

        const float playhead = processor.getDisplayPhase();
        if (playhead >= 0.0f)
        {
            g.setColour (text.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (toScreen (playhead, 0.0f).x), plot.getY(), plot.getBottom());
        }
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kickColour);
    g.drawText ("KICK (sidechain)", plot.reduced (6.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topRight);
    g.setColour (bassColour);
    g.drawText ("BASS (output)", plot.reduced (6.0f, 4.0f).translated (0.0f, 14.0f).removeFromTop (14.0f), juce::Justification::topRight);

    if (isZoomed())
    {
        g.setColour (text.withAlpha (0.7f));
        g.drawText ("zoom " + juce::String (juce::roundToInt (1.0f / (viewEnd - viewStart))) + "x  (Fit shows all)",
                    plot.reduced (6.0f, 4.0f).removeFromTop (14.0f), juce::Justification::topLeft);
    }

    if (processor.scopeFrozen.load())
    {
        g.setColour (kickColour);
        g.drawText ("FROZEN", plot.reduced (6.0f, 4.0f).removeFromBottom (14.0f), juce::Justification::bottomRight);
    }

    if (! curveUsed)
    {
        g.setColour (text.withAlpha (0.7f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Follower mode: the bass follows the kick level, the curve is not used",
                    plot, juce::Justification::centred);
        return;
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt().expanded ((int) pointRadius + 2));

    for (std::size_t i = 0; i + 1 < curve.size(); ++i)
    {
        if (! hasBendHandle (i))
            continue;

        const auto p = bendHandle (i);
        g.setColour (background);
        g.fillEllipse (juce::Rectangle<float> (bendRadius * 2.0f, bendRadius * 2.0f).withCentre (p));
        g.setColour (curveColour.withAlpha (0.8f));
        g.drawEllipse (juce::Rectangle<float> (bendRadius * 2.0f, bendRadius * 2.0f).withCentre (p), 1.5f);
    }

    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const auto p = toScreen (curve[i].x, curve[i].y);
        g.setColour (curveColour);
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
    using namespace psyduck::params;

    // Read the saved window size first: setting up the layout below overwrites it.
    const int savedWidth = processor.editorWidth.load();
    const int savedHeight = processor.editorHeight.load();

    addAndMakeVisible (curveEditor);

    // Header ------------------------------------------------------------------
    modeBox.addItemList ({ "Sync", "Audio trigger", "MIDI trigger", "Follower" }, 1);
    modeBox.setTooltip ("What starts the curve");
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboAttachment> (processor.apvts, mode, modeBox);

    targetBox.addItemList ({ "Volume", "Filter", "Pan" }, 1);
    targetBox.setTooltip ("What the curve moves: the volume, a low-pass filter or the stereo position");
    addAndMakeVisible (targetBox);
    targetAttachment = std::make_unique<ComboAttachment> (processor.apvts, target, targetBox);

    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    rebuildPresetMenu();

    setupButton (slotA, "A", "Setting A. Switch between A and B to compare two versions");
    setupButton (slotB, "B", "Setting B. Switch between A and B to compare two versions");
    setupButton (slotCopy, "A > B", "Copy the current setting onto the other one");
    setToggleLook (slotA);
    setToggleLook (slotB);
    slotA.onClick = [this] { if (processor.getActiveSlot() != 0) processor.switchSlot(); };
    slotB.onClick = [this] { if (processor.getActiveSlot() != 1) processor.switchSlot(); };
    slotCopy.onClick = [this]
    {
        processor.copyToOtherSlot();
        showMessage (processor.getActiveSlot() == 0 ? "Copied A to B." : "Copied B to A.");
    };

    // Toolbar -----------------------------------------------------------------
    for (int b = 0; b < 3; ++b)
    {
        auto& tab = bandTabs[(size_t) b];
        setupButton (tab, bandNames[b], juce::String ("Edit the curve of the ") + bandNames[b] + " band");
        tab.setColour (juce::TextButton::buttonOnColourId, bandColours[b].withAlpha (0.45f));
        tab.onClick = [this, b] { processor.editBand.store (b); };
    }

    setupButton (undoButton, "Undo");
    setupButton (redoButton, "Redo");
    setupButton (snapButton, "Snap", "Points stick to the grid. Shift flips this for one drag");
    setupButton (suggestButton, "Suggest", "Builds a starting curve from the kick shown under the curve");
    setupButton (copyButton, "Copy", "Copy this curve. Paste it in another PsyDuck or another band");
    setupButton (pasteButton, "Paste", "Paste a copied curve here");
    setupButton (waveButton, "Wave", "Show kick and bass as waveforms instead of levels");
    setupButton (freezeButton, "Freeze", "Hold the kick and bass picture still");
    setupButton (fitButton, "Fit", "Show the whole cycle (mouse wheel zooms, Shift + wheel scrolls)");

    undoButton.onClick = [this] { processor.undoCurve(); };
    redoButton.onClick = [this] { processor.redoCurve(); };
    suggestButton.onClick = [this] { suggestCurve(); };
    copyButton.onClick = [this] { copyCurve(); };
    pasteButton.onClick = [this] { pasteCurve(); };
    fitButton.onClick = [this] { curveEditor.resetZoom(); };

    for (auto* toggle : { &snapButton, &waveButton, &freezeButton })
    {
        toggle->setClickingTogglesState (true);
        setToggleLook (*toggle);
    }

    snapButton.setToggleState (processor.snapToGrid.load(), juce::dontSendNotification);
    snapButton.onClick = [this]
    {
        processor.snapToGrid.store (snapButton.getToggleState());
        processor.curveEditFinished();
    };

    waveButton.onClick = [this] { curveEditor.showWaveform = waveButton.getToggleState(); };
    freezeButton.setToggleState (processor.scopeFrozen.load(), juce::dontSendNotification);
    freezeButton.onClick = [this] { processor.scopeFrozen.store (freezeButton.getToggleState()); };

    for (int columns : { 4, 8, 16, 32, 64 })
        gridBox.addItem ("Grid " + juce::String (columns), columns);
    gridBox.setSelectedId (processor.gridColumns.load(), juce::dontSendNotification);
    gridBox.setTooltip ("How many steps the grid has across the cycle");
    gridBox.onChange = [this]
    {
        if (gridBox.getSelectedId() > 0)
        {
            processor.gridColumns.store (gridBox.getSelectedId());
            processor.curveEditFinished();
        }
    };
    addAndMakeVisible (gridBox);

    hint.setFont (juce::FontOptions (12.0f));
    hint.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hint);

    // Main controls -----------------------------------------------------------
    setupLabel (rateLabel, "Rate");
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (rate)))
        rateBox.addItemList (choice->choices, 1);
    addAndMakeVisible (rateBox);
    rateAttachment = std::make_unique<ComboAttachment> (processor.apvts, rate, rateBox);

    setupLabel (noteLabel, "Note");
    noteBox.addItem ("Any", 1);
    for (int note = 0; note < 128; ++note)
        noteBox.addItem (juce::MidiMessage::getMidiNoteName (note, true, true, 3), note + 2);
    addChildComponent (noteBox);
    noteLabel.setVisible (false);
    noteAttachment = std::make_unique<ComboAttachment> (processor.apvts, midiNote, noteBox);

    setupKnob (amount, psyduck::params::amount, "Amount", " %");
    setupKnob (mix, psyduck::params::mix, "Mix", " %");
    setupKnob (phase, psyduck::params::phase, "Phase");
    setupKnob (threshold, psyduck::params::threshold, "Threshold", " dB");
    setupKnob (retrigger, psyduck::params::retrigger, "Retrigger", " ms");
    setupKnob (release, psyduck::params::release, "Release", " ms");
    setupKnob (lookahead, psyduck::params::lookahead, "Lookahead", " ms");
    setupKnob (bassDelay, psyduck::params::bassDelay, "Bass delay", " ms");
    bassDelay.slider.setTooltip ("Shifts the bass later by a few milliseconds so its low end lines up with the kick");

    setupLabel (phaseLabel, "Kick / Bass phase");
    addAndMakeVisible (phaseMeter);

    for (auto* button : { &multibandButton, &invertButton })
    {
        button->setColour (juce::ToggleButton::textColourId, text);
        button->setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (*button);
    }

    multibandButton.setTooltip ("Split the bass into Low, Mid and High, each with its own curve and amount");
    invertButton.setTooltip ("Flip the polarity of the bass. Try it when the meter says cancelling");
    multibandAttachment = std::make_unique<ButtonAttachment> (processor.apvts, multiband, multibandButton);
    invertAttachment = std::make_unique<ButtonAttachment> (processor.apvts, invert, invertButton);

    setupButton (alignButton, "Align", "Finds the Bass delay and Invert setting that give the fullest low end. Let kick and bass play first");
    alignButton.onClick = [this] { alignPhase(); };

    // Second row --------------------------------------------------------------
    setupKnob (crossoverLow, crossover, "Low | Mid", " Hz");
    setupKnob (crossoverHigh, psyduck::params::crossoverHigh, "Mid | High", " Hz");
    setupKnob (amountLow, psyduck::params::amountLow, "Low", " %");
    setupKnob (amountMid, psyduck::params::amountMid, "Mid", " %");
    setupKnob (amountHigh, psyduck::params::amountHigh, "High", " %");
    setupKnob (filterMin, psyduck::params::filterMin, "Filter low point", " Hz");
    setupKnob (resonance, psyduck::params::resonance, "Resonance", " %");

    amountLow.label.setColour (juce::Label::textColourId, bandColours[0]);
    amountMid.label.setColour (juce::Label::textColourId, bandColours[1]);
    amountHigh.label.setColour (juce::Label::textColourId, bandColours[2]);
    crossoverLow.slider.setTooltip ("Frequency where Low ends and Mid begins");
    crossoverHigh.slider.setTooltip ("Frequency where Mid ends and High begins");
    filterMin.slider.setTooltip ("Where the filter sits when the curve is at the bottom. At the top it is fully open");

    panInfo.setText ("Pan: the middle of the curve is the centre, the bottom is left and the top is right. Amount sets the width.",
                     juce::dontSendNotification);
    panInfo.setColour (juce::Label::textColourId, text.withAlpha (0.7f));
    panInfo.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (panInfo);

    updateLayoutState();
    showMessage ({});
    startTimerHz (10);

    setResizable (true, true);
    setResizeLimits (940, 600, 2400, 1500);
    setSize (juce::jlimit (940, 2400, savedWidth), juce::jlimit (600, 1500, savedHeight));
}

void PsyDuckAudioProcessorEditor::setupButton (juce::TextButton& button, const juce::String& title, const juce::String& tooltip)
{
    button.setButtonText (title);
    button.setTooltip (tooltip);
    button.setColour (juce::TextButton::buttonColourId, panel);
    button.setColour (juce::TextButton::textColourOffId, text);
    button.setColour (juce::TextButton::textColourOnId, text);
    addAndMakeVisible (button);
}

void PsyDuckAudioProcessorEditor::setToggleLook (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
}

void PsyDuckAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& title)
{
    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (label);
}

void PsyDuckAudioProcessorEditor::setupKnob (Knob& knob, const char* paramId, const juce::String& title, const juce::String& suffix)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setColour (juce::Slider::thumbColourId, accent);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, text);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob.slider);

    setupLabel (knob.label, title);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramId, knob.slider);

    if (suffix.isNotEmpty())
        knob.slider.setTextValueSuffix (suffix);
}

void PsyDuckAudioProcessorEditor::timerCallback()
{
    const int mode = currentMode (processor);
    const int target = currentTarget (processor);
    const bool multiband = multibandButton.getToggleState();
    const int band = processor.editBand.load();
    const int slot = processor.getActiveSlot();

    if (mode != shownMode || target != shownTarget || multiband != shownMultiband || band != shownBand || slot != shownSlot)
        updateLayoutState();

    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
    fitButton.setEnabled (curveEditor.isZoomed());

    if (snapButton.getToggleState() != processor.snapToGrid.load())
        snapButton.setToggleState (processor.snapToGrid.load(), juce::dontSendNotification);

    if (gridBox.getSelectedId() != processor.gridColumns.load())
        gridBox.setSelectedId (processor.gridColumns.load(), juce::dontSendNotification);

    if (messageTicks > 0 && --messageTicks == 0)
        showMessage ({});
}

void PsyDuckAudioProcessorEditor::updateLayoutState()
{
    using namespace psyduck::params;

    shownMode = currentMode (processor);
    shownTarget = currentTarget (processor);
    shownMultiband = multibandButton.getToggleState();
    shownBand = processor.editBand.load();
    shownSlot = processor.getActiveSlot();

    const bool sync = shownMode == modeSync;
    const bool audio = shownMode == modeAudio;
    const bool midi = shownMode == modeMidi;
    const bool follow = shownMode == modeFollower;
    const bool volume = shownTarget == targetVolume;
    const bool filter = shownTarget == targetFilter;
    const bool bands = volume && shownMultiband;

    // Only the controls that do something right now stay lit.
    phase.setActive (sync);
    threshold.setActive (audio || follow);

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
    copyButton.setEnabled (! follow);
    pasteButton.setEnabled (! follow);

    // Second row follows Target.
    multibandButton.setVisible (volume);
    for (auto* knob : { &crossoverLow, &crossoverHigh, &amountLow, &amountMid, &amountHigh })
    {
        knob->setShown (volume);
        knob->setActive (bands);
    }
    filterMin.setShown (filter);
    resonance.setShown (filter);
    panInfo.setVisible (shownTarget == targetPan);

    for (int b = 0; b < 3; ++b)
    {
        bandTabs[(size_t) b].setVisible (bands);
        bandTabs[(size_t) b].setToggleState (b == shownBand, juce::dontSendNotification);
    }

    slotA.setToggleState (shownSlot == 0, juce::dontSendNotification);
    slotB.setToggleState (shownSlot == 1, juce::dontSendNotification);
    slotCopy.setButtonText (shownSlot == 0 ? "A > B" : "B > A");

    resized();

    if (messageTicks == 0)
        showMessage ({});
}

void PsyDuckAudioProcessorEditor::showMessage (const juce::String& message)
{
    if (message.isNotEmpty())
    {
        hint.setText (message, juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, kickColour);
        messageTicks = 60;
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
            hint.setText ("Drag points. Double-click adds or removes a point. Drag the small ring to bend. Shift flips Snap. Wheel zooms.",
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

    const int band = bandsActive (processor) ? processor.editBand.load() : 0;
    processor.beginCurveEdit (band);
    processor.setCurve (loaded, band);
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
        const int band = bandsActive (safeThis->processor) ? safeThis->processor.editBand.load() : 0;

        if (! folder.createDirectory().wasOk()
            || ! file.replaceWithText (PsyDuckAudioProcessor::curveToString (safeThis->processor.getCurve (band))))
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

    const int band = bandsActive (processor) ? processor.editBand.load() : 0;
    processor.beginCurveEdit (band);
    processor.setCurve (suggestion, band);
    processor.curveEditFinished();
    showMessage ("Curve built from the kick. Adjust the points to taste, or press Undo.");
}

void PsyDuckAudioProcessorEditor::copyCurve()
{
    const int band = bandsActive (processor) ? processor.editBand.load() : 0;
    juce::SystemClipboard::copyTextToClipboard (clipboardPrefix + PsyDuckAudioProcessor::curveToString (processor.getCurve (band)));
    showMessage ("Curve copied. Paste it in another PsyDuck or another band.");
}

void PsyDuckAudioProcessorEditor::pasteCurve()
{
    const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
    psyduck::Curve pasted;

    if (! clip.startsWith (clipboardPrefix)
        || ! PsyDuckAudioProcessor::curveFromString (clip.fromFirstOccurrenceOf (clipboardPrefix, false, false), pasted))
    {
        showMessage ("Nothing to paste. Press Copy on a PsyDuck curve first.");
        return;
    }

    const int band = bandsActive (processor) ? processor.editBand.load() : 0;
    processor.beginCurveEdit (band);
    processor.setCurve (pasted, band);
    processor.curveEditFinished();
    showMessage ("Curve pasted.");
}

void PsyDuckAudioProcessorEditor::alignPhase()
{
    float lagMs = 0.0f;
    const auto result = processor.findAlignment (lagMs);

    if (! result.valid)
    {
        showMessage ("Nothing to align yet. Route the kick to the sidechain input and let kick and bass play for a few seconds.");
        return;
    }

    const auto set = [this] (const char* id, float value)
    {
        if (auto* p = processor.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    set (psyduck::params::bassDelay, lagMs);
    set (psyduck::params::invert, result.invert ? 1.0f : 0.0f);

    showMessage ("Aligned: bass delay " + juce::String (lagMs, 2) + " ms, Invert " + (result.invert ? "on" : "off")
                 + ". Low-end match " + juce::String (juce::roundToInt (result.after * 100.0f)) + " %.");
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
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    processor.editorWidth.store (getWidth());
    processor.editorHeight.store (getHeight());

    auto area = getLocalBounds().reduced (16);

    // Header: title | A B copy ............ Mode | Target | Presets
    auto header = area.removeFromTop (28);
    header.removeFromLeft (104);
    slotA.setBounds (header.removeFromLeft (34));
    header.removeFromLeft (4);
    slotB.setBounds (header.removeFromLeft (34));
    header.removeFromLeft (4);
    slotCopy.setBounds (header.removeFromLeft (62));

    presetBox.setBounds (header.removeFromRight (180));
    header.removeFromRight (8);
    targetBox.setBounds (header.removeFromRight (100));
    header.removeFromRight (8);
    modeBox.setBounds (header.removeFromRight (130));
    area.removeFromTop (8);

    // Toolbar: band tabs | edit tools ............ view tools
    auto toolbar = area.removeFromTop (26);

    if (bandTabs[0].isVisible())
    {
        for (auto& tab : bandTabs)
        {
            tab.setBounds (toolbar.removeFromLeft (50));
            toolbar.removeFromLeft (4);
        }
        toolbar.removeFromLeft (10);
    }

    const auto place = [&toolbar] (juce::Component& c, int width)
    {
        c.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (5);
    };

    place (undoButton, 54);
    place (redoButton, 54);
    place (snapButton, 54);
    place (gridBox, 86);
    place (suggestButton, 72);
    place (copyButton, 54);
    place (pasteButton, 56);

    fitButton.setBounds (toolbar.removeFromRight (44));
    toolbar.removeFromRight (5);
    freezeButton.setBounds (toolbar.removeFromRight (66));
    toolbar.removeFromRight (5);
    waveButton.setBounds (toolbar.removeFromRight (56));
    area.removeFromTop (8);

    auto secondRow = area.removeFromBottom (96);
    area.removeFromBottom (6);
    auto firstRow = area.removeFromBottom (104);
    area.removeFromBottom (6);
    hint.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (4);
    curveEditor.setBounds (area);

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

    // First row: the main controls, then the phase tools.
    const int column = firstRow.getWidth() / 9;
    const auto next = [&] { return firstRow.removeFromLeft (column); };

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

    auto phaseSlot = next();
    phaseLabel.setBounds (phaseSlot.removeFromTop (20));
    auto phaseButtons = phaseSlot.removeFromBottom (24);
    invertButton.setBounds (phaseButtons.removeFromLeft (phaseButtons.getWidth() / 2 + 8));
    alignButton.setBounds (phaseButtons.reduced (2, 1));
    phaseMeter.setBounds (phaseSlot.reduced (2, 4));

    placeKnob (firstRow, bassDelay);

    // Second row: what belongs to the chosen Target.
    const int column2 = secondRow.getWidth() / 9;
    auto first = secondRow.removeFromLeft (column2);
    multibandButton.setBounds (first.withSizeKeepingCentre (first.getWidth() - 4, 24));

    auto filterArea = secondRow;
    panInfo.setBounds (first.getUnion (secondRow).reduced (4, 0));

    for (auto* knob : { &crossoverLow, &crossoverHigh, &amountLow, &amountMid, &amountHigh })
        placeKnob (secondRow.removeFromLeft (column2), *knob);

    placeKnob (filterArea.removeFromLeft (column2 + 20), filterMin);
    placeKnob (filterArea.removeFromLeft (column2), resonance);
}
