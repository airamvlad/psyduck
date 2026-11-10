#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
const juce::Identifier altSlotType { "ALT" };
const char* const curveProperties[] = { "curve", "curve1", "curve2" };
} // namespace

PsyDuckAudioProcessor::PsyDuckAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PsyDuck", psyduck::params::createLayout())
{
    using namespace psyduck::params;

    rateParam = apvts.getRawParameterValue (rate);
    amountParam = apvts.getRawParameterValue (amount);
    mixParam = apvts.getRawParameterValue (mix);
    phaseParam = apvts.getRawParameterValue (phase);
    modeParam = apvts.getRawParameterValue (mode);
    thresholdParam = apvts.getRawParameterValue (threshold);
    retriggerParam = apvts.getRawParameterValue (retrigger);
    lookaheadParam = apvts.getRawParameterValue (lookahead);
    midiNoteParam = apvts.getRawParameterValue (midiNote);
    multibandParam = apvts.getRawParameterValue (multiband);
    crossoverParam = apvts.getRawParameterValue (crossover);
    crossoverHighParam = apvts.getRawParameterValue (crossoverHigh);
    bandAmountParams = { apvts.getRawParameterValue (amountLow),
                         apvts.getRawParameterValue (amountMid),
                         apvts.getRawParameterValue (amountHigh) };
    invertParam = apvts.getRawParameterValue (invert);
    releaseParam = apvts.getRawParameterValue (release);
    targetParam = apvts.getRawParameterValue (target);
    filterMinParam = apvts.getRawParameterValue (filterMin);
    resonanceParam = apvts.getRawParameterValue (resonance);
    bassDelayParam = apvts.getRawParameterValue (bassDelay);

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
    using namespace psyduck::params;

    sync.prepare (sampleRate);
    detector.prepare (sampleRate);
    follower.prepare (sampleRate);
    phaseMeter.prepare (sampleRate);

    for (auto& g : gains)
        g.prepare (sampleRate);
    for (auto& shot : oneShots)
        shot.reset();
    for (auto& split : bandSplit)
        split.reset();
    for (auto& filter : shaperFilter)
        filter.reset();

    panSmoothed = 0.0f;
    panCoeff = (float) (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));
    lastSplit = false;
    lastFilter = false;
    lastCrossoverLow = lastCrossoverHigh = 0.0f;
    filterCounter = 0;

    const int maxDelay = (int) std::ceil ((double) (maxLookaheadMs + maxBassDelayMs) * 0.001 * sampleRate) + 1;
    for (auto& d : mainDelay)
        d.prepare (maxDelay);
    sidechainDelay.prepare (maxDelay);
    phaseBassDelay.prepare (maxDelay);

    // About 6 kHz is plenty for comparing the low end of kick and bass.
    alignDecimation = juce::jmax (1, juce::roundToInt (sampleRate / 6000.0));
    alignSampleRate = sampleRate / (double) alignDecimation;
    for (int i = 0; i < 2; ++i)
    {
        alignKickFilter[i].set (false, 150.0, sampleRate);
        alignBassFilter[i].set (false, 150.0, sampleRate);
        alignKickFilter[i].reset();
        alignBassFilter[i].reset();
    }

    {
        const juce::SpinLock::ScopedLockType lock (alignLock);
        const auto length = (size_t) juce::roundToInt (alignSampleRate * 2.0);
        alignKick.assign (length, 0.0f);
        alignBass.assign (length, 0.0f);
        alignWrite = 0;
        alignCounter = 0;
    }

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
    using namespace psyduck::params;

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

    const auto load = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };

    const auto rateIndex = juce::jlimit (0, (int) psyduck::rateBeats.size() - 1, (int) load (rateParam));
    const double beatsPerCycle = psyduck::rateBeats[(size_t) rateIndex];
    const float amount = load (amountParam) * 0.01f;
    const float mix = load (mixParam) * 0.01f;
    const double offset = (double) load (phaseParam) / 360.0;
    const int mode = juce::roundToInt (load (modeParam));
    const int target = juce::roundToInt (load (targetParam));
    const float threshold = juce::Decibels::decibelsToGain (load (thresholdParam));
    const int retriggerSamples = juce::roundToInt ((double) load (retriggerParam) * 0.001 * sampleRate);
    const int noteFilter = juce::roundToInt (load (midiNoteParam));
    const float polarity = load (invertParam) > 0.5f ? -1.0f : 1.0f;
    const double filterLow = load (filterMinParam);
    const double resonance = load (resonanceParam) * 0.01;

    const int lookahead = lookaheadSamplesFor (sampleRate);
    const int bassDelaySamples = juce::roundToInt ((double) juce::jlimit (0.0f, maxBassDelayMs, load (bassDelayParam)) * 0.001 * sampleRate);
    const int mainDelaySamples = juce::jmin (lookahead + bassDelaySamples, mainDelay[0].getMaxDelay());

    // Bands only make sense for volume; filter and pan work on the whole signal.
    const bool split = load (multibandParam) > 0.5f && target == targetVolume;
    const bool filtering = target == targetFilter;
    const bool panning = target == targetPan && numChannels == 2;
    const int bands = split ? numBands : 1;

    float bandAmount[numBands] = { amount, amount, amount };
    if (split)
        for (int b = 0; b < numBands; ++b)
            bandAmount[b] = amount * load (bandAmountParams[(size_t) b]) * 0.01f;

    follower.setRelease (load (releaseParam));

    if (split)
    {
        const float low = load (crossoverParam);
        const float high = juce::jmax (load (crossoverHighParam), low * 1.5f);

        if (! lastSplit || ! juce::approximatelyEqual (low, lastCrossoverLow) || ! juce::approximatelyEqual (high, lastCrossoverHigh))
        {
            for (auto& s : bandSplit)
            {
                s.setFrequencies (low, high, sampleRate);
                if (! lastSplit)
                    s.reset();
            }
            lastCrossoverLow = low;
            lastCrossoverHigh = high;
        }
    }
    lastSplit = split;

    if (filtering && ! lastFilter)
    {
        for (auto& f : shaperFilter)
            f.reset();
        filterCounter = 0;
    }
    lastFilter = filtering;

    if (curveDirty.load (std::memory_order_acquire))
    {
        const juce::SpinLock::ScopedTryLockType lock (curveLock);
        if (lock.isLocked())
        {
            audioCurves = sharedCurves;
            curveDirty.store (false, std::memory_order_release);
        }
    }

    if (mode != lastMode)
    {
        for (auto& shot : oneShots)
            shot.reset();
        detector.reset();
        follower.reset();
        lastMode = mode;
    }

    sync.beginBlock (bpm, beatsPerCycle, playing, ppq);
    for (auto& shot : oneShots)
        shot.setLength (beatsPerCycle * 60.0 / bpm * sampleRate);

    // The audio leaves the plugin `lookahead` samples late, so the synced curve
    // is shifted by the same amount to stay on the grid.
    const double syncOffset = offset - (double) lookahead * sync.getIncrement();

    const juce::SpinLock::ScopedTryLockType alignGuard (alignLock);
    const bool recording = alignGuard.isLocked() && ! alignKick.empty() && numSidechain > 0;
    const bool frozen = scopeFrozen.load (std::memory_order_relaxed);

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
        const float sidechainMono = numSidechain > 0 ? sidechainSum / (float) numSidechain : 0.0f;

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

        float curveValue[numBands] = { 1.0f, 1.0f, 1.0f };
        bool idle = false;

        if (mode == modeSync)
        {
            for (int b = 0; b < bands; ++b)
                curveValue[b] = audioCurves[(size_t) b].evaluate (syncPhase);
            displayed = syncPhase;
        }
        else if (mode == modeFollower)
        {
            for (int b = 0; b < bands; ++b)
                curveValue[b] = followed;
            displayed = syncPhase;
        }
        else
        {
            const bool trigger = mode == modeAudio ? hit : noteOn;

            for (int b = 0; b < numBands; ++b)
            {
                if (trigger)
                    oneShots[(size_t) b].trigger (lookahead);

                const float v = oneShots[(size_t) b].next (audioCurves[(size_t) b]);
                if (b < bands)
                    curveValue[b] = v;
            }

            idle = oneShots[0].isIdle();
            displayed = oneShots[0].getPhase();
        }

        float g[numBands];
        for (int b = 0; b < numBands; ++b)
            g[b] = gains[(size_t) b].next (b < bands ? curveValue[b] : 1.0f, bandAmount[b], mix);

        float panLeft = 1.0f, panRight = 1.0f;
        if (panning)
        {
            // Curve middle is centre, bottom is left, top is right. Waiting for a trigger: centre.
            const float position = idle ? 0.0f : (curveValue[0] * 2.0f - 1.0f) * amount * mix;
            panSmoothed += (position - panSmoothed) * panCoeff;
            panLeft = juce::jmin (1.0f, 1.0f - panSmoothed);
            panRight = juce::jmin (1.0f, 1.0f + panSmoothed);
        }

        if (filtering && --filterCounter <= 0)
        {
            filterCounter = 8;
            const double cutoff = psyduck::ShaperFilter::cutoffFor (g[0], filterLow);
            for (auto& f : shaperFilter)
                f.set (cutoff, resonance, sampleRate);
        }

        float outputSum = 0.0f, inputSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = mainBuffer.getWritePointer (ch);
            const float in = data[i] * polarity;
            inputSum += in;

            const float delayed = mainDelay[(size_t) ch].process (in, mainDelaySamples);
            float out = 0.0f;

            if (split)
            {
                float low = 0.0f, mid = 0.0f, high = 0.0f;
                bandSplit[(size_t) ch].process (delayed, low, mid, high);
                out = low * g[0] + mid * g[1] + high * g[2];
            }
            else if (filtering)
            {
                out = shaperFilter[(size_t) ch].process (delayed);
            }
            else if (panning)
            {
                out = delayed * (ch == 0 ? panLeft : panRight);
            }
            else
            {
                out = delayed * g[0];
            }

            data[i] = out;
            outputSum += out;
        }

        const float inputMono = numChannels > 0 ? inputSum / (float) numChannels : 0.0f;
        const float outputMono = numChannels > 0 ? outputSum / (float) numChannels : 0.0f;

        // Phase check uses the bass before ducking, where it overlaps the kick,
        // with the Bass Delay applied so the meter shows the effect of the knob.
        const float shiftedBass = phaseBassDelay.process (inputMono, bassDelaySamples);
        if (numSidechain > 0)
            phaseMeter.process (sidechainMono, shiftedBass);

        if (recording)
        {
            const float k = alignKickFilter[1].process (alignKickFilter[0].process (sidechainMono));
            const float b = alignBassFilter[1].process (alignBassFilter[0].process (inputMono * polarity));   // undo Invert: record the raw bass

            if (++alignCounter >= alignDecimation)
            {
                alignCounter = 0;
                alignKick[(size_t) alignWrite] = k;
                alignBass[(size_t) alignWrite] = b;
                if (++alignWrite >= (int) alignKick.size())
                    alignWrite = 0;
            }
        }

        const float delayedSidechain = sidechainDelay.process (sidechainMono, lookahead);

        if (displayed >= 0.0f && ! frozen)
            scope.write (displayed, delayedSidechain, outputMono);
    }

    phaseValid.store (numSidechain > 0 && phaseMeter.hasSignal(), std::memory_order_relaxed);
    phaseCorrelation.store (phaseMeter.getCorrelation(), std::memory_order_relaxed);
    displayPhase.store (displayed, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PsyDuckAudioProcessor::createEditor()
{
    return new PsyDuckAudioProcessorEditor (*this);
}

//==============================================================================
psyduck::Curve PsyDuckAudioProcessor::getCurve (int band) const
{
    const juce::SpinLock::ScopedLockType lock (curveLock);
    return sharedCurves[(size_t) juce::jlimit (0, numBands - 1, band)];
}

void PsyDuckAudioProcessor::setCurve (const psyduck::Curve& newCurve, int band)
{
    {
        const juce::SpinLock::ScopedLockType lock (curveLock);
        sharedCurves[(size_t) juce::jlimit (0, numBands - 1, band)] = newCurve;
        curveDirty.store (true, std::memory_order_release);
    }

    curveVersion.fetch_add (1, std::memory_order_relaxed);
}

void PsyDuckAudioProcessor::beginCurveEdit (int band)
{
    undoStack.push_back ({ band, getCurve (band) });
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void PsyDuckAudioProcessor::undoCurve()
{
    if (undoStack.empty())
        return;

    const auto step = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ step.band, getCurve (step.band) });
    setCurve (step.curve, step.band);
    editBand.store (step.band);
    curveEditFinished();
}

void PsyDuckAudioProcessor::redoCurve()
{
    if (redoStack.empty())
        return;

    const auto step = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ step.band, getCurve (step.band) });
    setCurve (step.curve, step.band);
    editBand.store (step.band);
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

//==============================================================================
psyduck::Alignment PsyDuckAudioProcessor::findAlignment (float& lagMs)
{
    std::vector<float> kick, bass;
    double rate = 6000.0;

    {
        const juce::SpinLock::ScopedLockType lock (alignLock);
        const auto n = alignKick.size();
        kick.resize (n);
        bass.resize (n);

        // Unroll the ring so the oldest sample comes first.
        for (size_t i = 0; i < n; ++i)
        {
            const auto from = ((size_t) alignWrite + i) % n;
            kick[i] = alignKick[from];
            bass[i] = alignBass[from];
        }

        rate = alignSampleRate;
    }

    const int maxLag = juce::roundToInt ((double) psyduck::params::maxBassDelayMs * 0.001 * rate);
    const auto result = psyduck::findBestAlignment (kick, bass, maxLag);
    lagMs = (float) ((double) result.lag * 1000.0 / rate);
    return result;
}

//==============================================================================
juce::ValueTree PsyDuckAudioProcessor::createState() const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState();

    // A leftover copy of the other A/B slot must never nest inside this one.
    while (state.getChildWithName (altSlotType).isValid())
        state.removeChild (state.getChildWithName (altSlotType), nullptr);

    for (int b = 0; b < numBands; ++b)
        state.setProperty (curveProperties[b], curveToString (getCurve (b)), nullptr);

    state.setProperty ("snap", snapToGrid.load(), nullptr);
    state.setProperty ("grid", gridColumns.load(), nullptr);
    return state;
}

void PsyDuckAudioProcessor::applyState (const juce::ValueTree& source)
{
    auto state = source.createCopy();

    while (state.getChildWithName (altSlotType).isValid())
        state.removeChild (state.getChildWithName (altSlotType), nullptr);

    for (int b = 0; b < numBands; ++b)
    {
        psyduck::Curve loaded;
        if (curveFromString (state.getProperty (curveProperties[b]).toString(), loaded))
            setCurve (loaded, b);
    }

    snapToGrid.store ((bool) state.getProperty ("snap", true));
    gridColumns.store (juce::jlimit (4, 64, (int) state.getProperty ("grid", 16)));

    apvts.replaceState (state);
}

void PsyDuckAudioProcessor::switchSlot()
{
    slots[(size_t) activeSlot] = createState();
    activeSlot = 1 - activeSlot;

    // An empty slot starts as a copy of the other one.
    if (slots[(size_t) activeSlot].isValid())
        applyState (slots[(size_t) activeSlot]);

    undoStack.clear();
    redoStack.clear();
    curveEditFinished();
}

void PsyDuckAudioProcessor::copyToOtherSlot()
{
    slots[(size_t) (1 - activeSlot)] = createState();
    curveEditFinished();
}

void PsyDuckAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = createState();
    state.setProperty ("slot", activeSlot, nullptr);
    state.setProperty ("width", editorWidth.load(), nullptr);
    state.setProperty ("height", editorHeight.load(), nullptr);

    const auto& other = slots[(size_t) (1 - activeSlot)];
    if (other.isValid())
    {
        juce::ValueTree alt (altSlotType);
        alt.appendChild (other.createCopy(), nullptr);
        state.appendChild (alt, nullptr);
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PsyDuckAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    activeSlot = juce::jlimit (0, 1, (int) state.getProperty ("slot", 0));
    editorWidth.store (juce::jlimit (400, 4000, (int) state.getProperty ("width", editorWidth.load())));
    editorHeight.store (juce::jlimit (300, 3000, (int) state.getProperty ("height", editorHeight.load())));

    slots = {};
    const auto alt = state.getChildWithName (altSlotType);
    if (alt.isValid() && alt.getNumChildren() > 0)
        slots[(size_t) (1 - activeSlot)] = alt.getChild (0).createCopy();

    applyState (state);
    undoStack.clear();
    redoStack.clear();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsyDuckAudioProcessor();
}
