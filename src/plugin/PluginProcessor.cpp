#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

PsyDuckAudioProcessor::PsyDuckAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PsyDuck", psyduck::params::createLayout())
{
    rateParam = apvts.getRawParameterValue (psyduck::params::rate);
    amountParam = apvts.getRawParameterValue (psyduck::params::amount);
    mixParam = apvts.getRawParameterValue (psyduck::params::mix);
    phaseParam = apvts.getRawParameterValue (psyduck::params::phase);
    modeParam = apvts.getRawParameterValue (psyduck::params::mode);
    thresholdParam = apvts.getRawParameterValue (psyduck::params::threshold);
    retriggerParam = apvts.getRawParameterValue (psyduck::params::retrigger);
    lookaheadParam = apvts.getRawParameterValue (psyduck::params::lookahead);
    midiNoteParam = apvts.getRawParameterValue (psyduck::params::midiNote);
    multibandParam = apvts.getRawParameterValue (psyduck::params::multiband);
    crossoverParam = apvts.getRawParameterValue (psyduck::params::crossover);
    invertParam = apvts.getRawParameterValue (psyduck::params::invert);
    releaseParam = apvts.getRawParameterValue (psyduck::params::release);

    // Keeps the latency reported to the host in step with the Lookahead knob.
    startTimerHz (10);
}

int PsyDuckAudioProcessor::lookaheadSamplesFor (double sampleRate) const noexcept
{
    const float ms = juce::jlimit (0.0f, psyduck::params::maxLookaheadMs,
                                   lookaheadParam->load (std::memory_order_relaxed));
    return juce::roundToInt ((double) ms * 0.001 * sampleRate);
}

void PsyDuckAudioProcessor::timerCallback()
{
    const double sr = getSampleRate();
    if (sr <= 0.0)
        return;

    const int wanted = lookaheadSamplesFor (sr);
    if (wanted != getLatencySamples())
        setLatencySamples (wanted);
}

void PsyDuckAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sync.prepare (sampleRate);
    gain.prepare (sampleRate);
    detector.prepare (sampleRate);
    oneShot.reset();
    follower.prepare (sampleRate);
    phaseMeter.prepare (sampleRate);
    for (auto& split : bandSplit)
        split.reset();
    lastCrossover = 0.0f;
    lastMultiband = false;

    const int maxDelay = (int) std::ceil ((double) psyduck::params::maxLookaheadMs * 0.001 * sampleRate) + 1;
    for (auto& d : mainDelay)
        d.prepare (maxDelay);
    sidechainDelay.prepare (maxDelay);

    setLatencySamples (lookaheadSamplesFor (sampleRate));
}

bool PsyDuckAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    const auto sidechain = layouts.getChannelSet (true, 1);
    return sidechain.isDisabled()
        || sidechain == juce::AudioChannelSet::mono()
        || sidechain == juce::AudioChannelSet::stereo();
}

void PsyDuckAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    auto sidechainBuffer = getBusCount (true) > 1 ? getBusBuffer (buffer, true, 1)
                                                  : juce::AudioBuffer<float>();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (mainBuffer.getNumChannels(), (int) mainDelay.size());
    const int numSidechain = sidechainBuffer.getNumChannels();
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

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

    if (bpm <= 0.0)
        bpm = 120.0;

    const auto rateIndex = juce::jlimit (0, (int) psyduck::rateBeats.size() - 1,
                                         (int) rateParam->load (std::memory_order_relaxed));
    const double beatsPerCycle = psyduck::rateBeats[(size_t) rateIndex];
    const float amount = amountParam->load (std::memory_order_relaxed) * 0.01f;
    const float mix = mixParam->load (std::memory_order_relaxed) * 0.01f;
    const double offset = (double) phaseParam->load (std::memory_order_relaxed) / 360.0;
    const int mode = juce::roundToInt (modeParam->load (std::memory_order_relaxed));
    const float threshold = juce::Decibels::decibelsToGain (thresholdParam->load (std::memory_order_relaxed));
    const int retriggerSamples = juce::roundToInt ((double) retriggerParam->load (std::memory_order_relaxed) * 0.001 * sampleRate);
    const int lookahead = juce::jmin (lookaheadSamplesFor (sampleRate), mainDelay[0].getMaxDelay());

    const int noteFilter = juce::roundToInt (midiNoteParam->load (std::memory_order_relaxed));
    const bool multiband = multibandParam->load (std::memory_order_relaxed) > 0.5f;
    const float crossover = crossoverParam->load (std::memory_order_relaxed);
    const float polarity = invertParam->load (std::memory_order_relaxed) > 0.5f ? -1.0f : 1.0f;

    follower.setRelease (releaseParam->load (std::memory_order_relaxed));

    if (multiband && (! lastMultiband || ! juce::approximatelyEqual (crossover, lastCrossover)))
    {
        for (auto& split : bandSplit)
        {
            split.setFrequency (crossover, sampleRate);
            if (! lastMultiband)
                split.reset();
        }
        lastCrossover = crossover;
    }
    lastMultiband = multiband;

    if (curveDirty.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (curveLock);
        if (lock.isLocked())
        {
            audioCurve = sharedCurve;
            curveDirty.store (false, std::memory_order_release);
        }
    }

    if (mode != lastMode)
    {
        oneShot.reset();
        detector.reset();
        follower.reset();
        lastMode = mode;
    }

    sync.beginBlock (bpm, beatsPerCycle, playing, ppq);
    oneShot.setLength (beatsPerCycle * 60.0 / bpm * sampleRate);

    // The audio leaves the plugin `lookahead` samples late, so the synced curve
    // is shifted by the same amount to stay on the grid.
    const double syncOffset = offset - (double) lookahead * sync.getIncrement();

    auto midiIt = midi.begin();
    const auto midiEnd = midi.end();
    float displayed = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sidechainLevel = 0.0f, sidechainSum = 0.0f;
        for (int ch = 0; ch < numSidechain; ++ch)
        {
            const float v = sidechainBuffer.getReadPointer (ch)[i];
            sidechainLevel = juce::jmax (sidechainLevel, std::abs (v));
            sidechainSum += v;
        }

        bool noteOn = false;
        while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
        {
            const auto message = (*midiIt).getMessage();
            if (message.isNoteOn() && (noteFilter <= 0 || message.getNoteNumber() == noteFilter - 1))
                noteOn = true;
            ++midiIt;
        }

        // Keep every engine running so switching modes is seamless.
        const float syncPhase = sync.next (syncOffset);
        const bool hit = detector.process (sidechainLevel, threshold, retriggerSamples);
        const float followed = follower.process (sidechainLevel, threshold);

        float curveValue = 1.0f;

        if (mode == psyduck::params::modeSync)
        {
            curveValue = audioCurve.evaluate (syncPhase);
            displayed = syncPhase;
        }
        else if (mode == psyduck::params::modeFollower)
        {
            curveValue = followed;
            displayed = syncPhase;
        }
        else
        {
            if (mode == psyduck::params::modeAudio ? hit : noteOn)
                oneShot.trigger (lookahead);

            curveValue = oneShot.next (audioCurve);
            displayed = oneShot.getPhase();
        }

        const float g = gain.next (curveValue, amount, mix);

        float outputLevel = 0.0f, inputSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = mainBuffer.getWritePointer (ch);
            const float in = data[i] * polarity;
            inputSum += in;

            const float delayed = mainDelay[(size_t) ch].process (in, lookahead);
            float out = 0.0f;

            if (multiband)
            {
                // Only the low band is ducked, the rest passes untouched.
                float low = 0.0f, high = 0.0f;
                bandSplit[(size_t) ch].process (delayed, low, high);
                out = low * g + high;
            }
            else
            {
                out = delayed * g;
            }

            data[i] = out;
            outputLevel = juce::jmax (outputLevel, std::abs (out));
        }

        // Phase check uses the bass before ducking, where it overlaps the kick.
        if (numSidechain > 0 && numChannels > 0)
            phaseMeter.process (sidechainSum / (float) numSidechain, inputSum / (float) numChannels);

        const float delayedSidechain = sidechainDelay.process (sidechainLevel, lookahead);

        if (displayed >= 0.0f)
            scope.write (displayed, delayedSidechain, outputLevel);
    }

    phaseValid.store (numSidechain > 0 && phaseMeter.hasSignal(), std::memory_order_relaxed);
    phaseCorrelation.store (phaseMeter.getCorrelation(), std::memory_order_relaxed);
    displayPhase.store (displayed, std::memory_order_relaxed);
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

void PsyDuckAudioProcessor::beginCurveEdit()
{
    undoStack.push_back (getCurve());
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PsyDuckAudioProcessor::undoCurve()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (getCurve());
    setCurve (undoStack.back());
    undoStack.pop_back();
    curveEditFinished();
}

void PsyDuckAudioProcessor::redoCurve()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (getCurve());
    setCurve (redoStack.back());
    redoStack.pop_back();
    curveEditFinished();
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
    state.setProperty ("snap", snapToGrid.load(), nullptr);

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

            snapToGrid.store ((bool) state.getProperty ("snap", true));

            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyDuckAudioProcessor();
}
