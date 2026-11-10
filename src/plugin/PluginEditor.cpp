#include "PluginEditor.h"

#include "Parameters.h"

namespace
{
const juce::Colour background { 0xff14161c };
const juce::Colour panel { 0xff1d2029 };
const juce::Colour grid { 0xff2c313d };
const juce::Colour accent { 0xff7dffb0 };
const juce::Colour text { 0xffd7dbe6 };
} // namespace

CurveDisplay::CurveDisplay (PsyDuckAudioProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);

    const auto plot = area.reduced (10.0f);

    g.setColour (grid);
    for (int i = 0; i <= 4; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    const auto& curve = processor.getCurve();
    const int steps = juce::jmax (2, juce::roundToInt (plot.getWidth()));

    juce::Path line;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float x = plot.getX() + plot.getWidth() * t;
        const float y = plot.getBottom() - plot.getHeight() * curve.evaluate (t);

        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    juce::Path fill (line);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    g.setColour (accent.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (2.0f));

    const float playheadX = plot.getX() + plot.getWidth() * processor.getDisplayPhase();
    g.setColour (text.withAlpha (0.7f));
    g.drawVerticalLine (juce::roundToInt (playheadX), plot.getY(), plot.getBottom());
}

PsyDuckAudioProcessorEditor::PsyDuckAudioProcessorEditor (PsyDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curveDisplay (p)
{
    addAndMakeVisible (curveDisplay);

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

    setSize (560, 380);
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
    g.drawText ("PsyDuck", getLocalBounds().removeFromTop (40).reduced (16, 0), juce::Justification::centredLeft);
}

void PsyDuckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (28);

    auto controls = area.removeFromBottom (110);
    area.removeFromBottom (12);
    curveDisplay.setBounds (area);

    const int column = controls.getWidth() / 4;

    auto rateArea = controls.removeFromLeft (column);
    rateLabel.setBounds (rateArea.removeFromTop (20));
    rateBox.setBounds (rateArea.withSizeKeepingCentre (juce::jmin (110, rateArea.getWidth() - 10), 28));

    for (auto* knob : { &amount, &mix, &phase })
    {
        auto knobArea = controls.removeFromLeft (column);
        knob->label.setBounds (knobArea.removeFromTop (20));
        knob->slider.setBounds (knobArea);
    }
}
