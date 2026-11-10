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

    /** The curve currently in use. Fixed for now, editable from the next milestone. */
    const psyduck::Curve& getCurve() const noexcept { return curve; }

    /** Position in the cycle (0..1) at the end of the last block, for the UI playhead. */
    float getDisplayPhase() const noexcept { return displayPhase.load (std::memory_order_relaxed); }

private:
    psyduck::Curve curve;
    psyduck::SyncEngine sync;
    psyduck::GainProcessor gain;

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;

    std::atomic<float> displayPhase { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsyDuckAudioProcessor)
};
