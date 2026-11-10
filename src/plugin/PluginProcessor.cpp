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

    if (curveDirty.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (curveLock);
        if (lock.isLocked())
        {
            audioCurve = sharedCurve;
            curveDirty.store (false, std::memory_order_release);
        }
    }

    sync.beginBlock (bpm, psyduck::rateBeats[(size_t) rateIndex], playing, ppq);

    float lastPhase = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        lastPhase = sync.next (offset);
        const float g = gain.next (audioCurve.evaluate (lastPhase), amount, mix);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    displayPhase.store (lastPhase, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PsyDuckAudioProcessor::createEditor()
{
    return new PsyDuckAudioProcessorEditor (*this);
}

psyduck::Curve PsyDuckAudioProcessor::getCurve() const
{
    const juce::SpinLock::ScopedLockType lock (curveLock);
    return sharedCurve;
}

void PsyDuckAudioProcessor::setCurve (const psyduck::Curve& newCurve)
{
    {
        const juce::SpinLock::ScopedLockType lock (curveLock);
        sharedCurve = newCurve;
        curveDirty.store (true, std::memory_order_release);
    }

    curveVersion.fetch_add (1, std::memory_order_relaxed);
}

void PsyDuckAudioProcessor::curveEditFinished()
{
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

juce::String PsyDuckAudioProcessor::curveToString (const psyduck::Curve& c)
{
    juce::StringArray parts;
    for (std::size_t i = 0; i < c.size(); ++i)
        parts.add (juce::String (c[i].x, 6) + "," + juce::String (c[i].y, 6) + "," + juce::String (c[i].curve, 6));

    return parts.joinIntoString (";");
}

bool PsyDuckAudioProcessor::curveFromString (const juce::String& text, psyduck::Curve& c)
{
    psyduck::Curve parsed;
    parsed.clear();

    for (const auto& part : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto values = juce::StringArray::fromTokens (part, ",", "");
        if (values.size() != 3)
            return false;

        if (parsed.addPoint ({ values[0].getFloatValue(), values[1].getFloatValue(), values[2].getFloatValue() }) < 0)
            return false;
    }

    if (parsed.size() < 2)
        return false;

    // The editor relies on the curve spanning the whole cycle.
    if (parsed[0].x > 1.0e-4f || parsed[parsed.size() - 1].x < 1.0f - 1.0e-4f)
        return false;

    c = parsed;
    return true;
}

void PsyDuckAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("curve", curveToString (getCurve()), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PsyDuckAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml (*xml);

            psyduck::Curve loaded;
            if (curveFromString (state.getProperty ("curve").toString(), loaded))
                setCurve (loaded);

            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyDuckAudioProcessor();
}
