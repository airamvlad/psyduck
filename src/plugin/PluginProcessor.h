#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/Crossover.h"
#include "dsp/Curve.h"
#include "dsp/DelayLine.h"
#include "dsp/EnvelopeFollower.h"
#include "dsp/GainProcessor.h"
#include "dsp/OneShot.h"
#include "dsp/PhaseAlign.h"
#include "dsp/PhaseMeter.h"
#include "dsp/Scope.h"
#include "dsp/ShaperFilter.h"
#include "dsp/SyncEngine.h"
#include "dsp/TriggerDetector.h"

#include <array>
#include <atomic>
#include <vector>

class PsyDuckAudioProcessor final : public juce::AudioProcessor,
                                    private juce::Timer
{
public:
    static constexpr int numBands = psyduck::params::numBands;

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
    bool acceptsMidi() const override { return true; }
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

    //==========================================================================
    // Curves. Band 0 is the main curve (and the low band when Multiband is on),
    // band 1 the mid band, band 2 the high band.
    // All of these are safe from any thread except the audio thread.

    psyduck::Curve getCurve (int band = 0) const;
    void setCurve (const psyduck::Curve&, int band = 0);

    /** Tells the host the project changed. Call when the user finishes an edit. */
    void curveEditFinished();

    /** Goes up every time a curve changes, so the UI knows when to refresh. */
    int getCurveVersion() const noexcept { return curveVersion.load (std::memory_order_relaxed); }

    /** Call before changing a curve so the change can be undone. Message thread only. */
    void beginCurveEdit (int band = 0);
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undoCurve();
    void redoCurve();

    static juce::String curveToString (const psyduck::Curve&);
    static bool curveFromString (const juce::String&, psyduck::Curve&);

    //==========================================================================
    // Editor settings that are saved with the project.

    std::atomic<bool> snapToGrid { true };
    std::atomic<int> gridColumns { 16 };
    std::atomic<int> editBand { 0 };          // which curve the editor shows
    std::atomic<int> editorWidth { 1000 };
    std::atomic<int> editorHeight { 680 };

    //==========================================================================
    // A/B comparison. Message thread only.

    int getActiveSlot() const noexcept { return activeSlot; }
    void switchSlot();
    void copyToOtherSlot();

    //==========================================================================
    // Meters.

    /** How the low end of kick and bass line up: +1 adds up, -1 cancels.
        Only meaningful while hasPhaseReading() is true. */
    float getPhaseCorrelation() const noexcept { return phaseCorrelation.load (std::memory_order_relaxed); }
    bool hasPhaseReading() const noexcept { return phaseValid.load (std::memory_order_relaxed); }

    /** Works out the bass delay and polarity that give the fullest low end,
        from the last two seconds of audio. Message thread only.
        The result's lag is in milliseconds here (see alignmentLagMs). */
    psyduck::Alignment findAlignment (float& lagMs);

    /** Kick and bass along the cycle, for drawing under the curve. */
    const psyduck::Scope& getScope() const noexcept { return scope; }
    std::atomic<bool> scopeFrozen { false };

    /** Position in the cycle (0..1) at the end of the last block, for the UI playhead.
        Negative while a trigger mode is waiting for the next hit. */
    float getDisplayPhase() const noexcept { return displayPhase.load (std::memory_order_relaxed); }

private:
    void timerCallback() override;
    int lookaheadSamplesFor (double sampleRate) const noexcept;

    juce::ValueTree createState() const;
    void applyState (const juce::ValueTree&);

    struct UndoStep
    {
        int band = 0;
        psyduck::Curve curve;
    };

    // Curves
    std::array<psyduck::Curve, numBands> audioCurves;      // audio thread only
    std::array<psyduck::Curve, numBands> sharedCurves;     // guarded by curveLock
    mutable juce::SpinLock curveLock;
    std::atomic<bool> curveDirty { false };
    std::atomic<int> curveVersion { 0 };
    std::vector<UndoStep> undoStack, redoStack;            // message thread only

    // A/B
    std::array<juce::ValueTree, 2> slots;
    int activeSlot = 0;

    // DSP
    psyduck::SyncEngine sync;
    psyduck::TriggerDetector detector;
    psyduck::EnvelopeFollower follower;
    std::array<psyduck::OneShot, numBands> oneShots;
    std::array<psyduck::GainProcessor, numBands> gains;
    std::array<psyduck::DelayLine, 2> mainDelay;
    std::array<psyduck::ThreeBandSplit, 2> bandSplit;
    std::array<psyduck::ShaperFilter, 2> shaperFilter;
    psyduck::DelayLine sidechainDelay;
    psyduck::DelayLine phaseBassDelay;
    psyduck::PhaseMeter phaseMeter;
    psyduck::Scope scope;

    float panSmoothed = 0.0f;
    float panCoeff = 1.0f;
    float lastCrossoverLow = 0.0f, lastCrossoverHigh = 0.0f;
    bool lastSplit = false;
    bool lastFilter = false;
    int lastMode = 0;
    int filterCounter = 0;

    // Recording for phase alignment: low-passed, downsampled kick and bass.
    psyduck::Biquad alignKickFilter[2], alignBassFilter[2];
    std::vector<float> alignKick, alignBass;               // guarded by alignLock
    juce::SpinLock alignLock;
    int alignWrite = 0;
    int alignDecimation = 8;
    int alignCounter = 0;
    double alignSampleRate = 6000.0;

    std::atomic<float> phaseCorrelation { 0.0f };
    std::atomic<bool> phaseValid { false };
    std::atomic<float> displayPhase { 0.0f };

    // Parameters
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* retriggerParam = nullptr;
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* midiNoteParam = nullptr;
    std::atomic<float>* multibandParam = nullptr;
    std::atomic<float>* crossoverParam = nullptr;
    std::atomic<float>* crossoverHighParam = nullptr;
    std::array<std::atomic<float>*, numBands> bandAmountParams {};
    std::atomic<float>* invertParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* targetParam = nullptr;
    std::atomic<float>* filterMinParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* bassDelayParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessor)
};
