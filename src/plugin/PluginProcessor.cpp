#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

PsyDuckAudioProcessor::PsyDuckAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PsyDuck", psyduck::params::createLayout())
{
    rateParam = apvts.getRawParameterValue (psyduck::params::rate);
    amountParam = apvts.getRawParameterValue (psyduck::params::amount);
    mixParam = apvts.getRawParameterValue (psyduck::params::mix);
    phaseParam = apvts.getRawParameterValue (psyduck::params::phase);
}

void PsyDuckAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sync.prepare (sampleRate);
    gain.prepare (sampleRate);
}

bool PsyDuckAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void PsyDuckAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            bpm = position->getBpm().orFallback (120.0);
            ppq = position->getPpqPosition().orFallback (0.0);
            playing = position->getIsPlaying() && position->getPpqPosition().hasValue();
        }
    }

    const auto rateIndex = juce::jlimit (0, (int) psyduck::rateBeats.size() - 1,
                                         (int) rateParam->load (std::memory_order_relaxed));
    const float amount = amountParam->load (std::memory_order_relaxed) * 0.01f;
    const float mix = mixParam->load (std::memory_order_relaxed) * 0.01f;
    const double offset = (double) phaseParam->load (std::memory_order_relaxed) / 360.0;

    sync.beginBlock (bpm, psyduck::rateBeats[(size_t) rateIndex], playing, ppq);

    float lastPhase = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        lastPhase = sync.next (offset);
        const float g = gain.next (curve.evaluate (lastPhase), amount, mix);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    displayPhase.store (lastPhase, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PsyDuckAudioProcessor::createEditor()
{
    return new PsyDuckAudioProcessorEditor (*this);
}

void PsyDuckAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PsyDuckAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyDuckAudioProcessor();
}
