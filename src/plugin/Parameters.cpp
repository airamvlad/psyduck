#include "Parameters.h"

#include "dsp/SyncEngine.h"

namespace psyduck::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray rates;
    for (const auto* name : rateNames)
        rates.add (name);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { rate, 1 }, "Rate", rates, defaultRateIndex));

    const auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { amount, 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { phase, 1 }, "Phase",
        juce::NormalisableRange<float> (0.0f, 360.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel (juce::String::fromUTF8 ("\xc2\xb0"))));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { mode, 1 }, "Mode", juce::StringArray { "Sync", "Audio", "MIDI" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { threshold, 1 }, "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { retrigger, 1 }, "Retrigger",
        juce::NormalisableRange<float> (10.0f, 500.0f, 1.0f, 0.5f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { lookahead, 1 }, "Lookahead",
        juce::NormalisableRange<float> (0.0f, maxLookaheadMs, 0.1f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    return layout;
}

} // namespace psyduck::params
