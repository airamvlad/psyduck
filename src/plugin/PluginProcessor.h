#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Curve.h"
#include "dsp/GainProcessor.h"
#include "dsp/SyncEngine.h"

#include <atomic>

class PsyDuckAudioProcessor final : public juce::AudioProcessor
{
public:
    PsyDuckAudioProcessor();
    ~PsyDuckAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Copy of the current curve. Safe to call from any thread except the audio thread. */
    psyduck::Curve getCurve() const;

    /** Replaces the curve. Safe to call from any thread except the audio thread. */
    void setCurve (const psyduck::Curve&);

    /** Tells the host the project changed. Call when the user finishes an edit. */
    void curveEditFinished();

    /** Goes up every time the curve changes, so the UI knows when to refresh. */
    int getCurveVersion() const noexcept { return curveVersion.load (std::memory_order_relaxed); }

    /** Position in the cycle (0..1) at the end of the last block, for the UI playhead. */
    float getDisplayPhase() const noexcept { return displayPhase.load (std::memory_order_relaxed); }

private:
    static juce::String curveToString (const psyduck::Curve&);
    static bool curveFromString (const juce::String&, psyduck::Curve&);

    psyduck::Curve audioCurve;          // audio thread only
    psyduck::Curve sharedCurve;         // guarded by curveLock
    mutable juce::SpinLock curveLock;
    std::atomic<bool> curveDirty { false };
    std::atomic<int> curveVersion { 0 };

    psyduck::SyncEngine sync;
    psyduck::GainProcessor gain;

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;

    std::atomic<float> displayPhase { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessor)
};
