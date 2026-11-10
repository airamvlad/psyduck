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

    return layout;
}

} // namespace psyduck::params
