#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace psyduck::params
{
inline constexpr const char* rate = "rate";
inline constexpr const char* amount = "amount";
inline constexpr const char* mix = "mix";
inline constexpr const char* phase = "phase";
inline constexpr const char* mode = "mode";
inline constexpr const char* threshold = "threshold";
inline constexpr const char* retrigger = "retrigger";
inline constexpr const char* lookahead = "lookahead";

inline constexpr const char* midiNote = "midinote";
inline constexpr const char* multiband = "multiband";
inline constexpr const char* crossover = "crossover";
inline constexpr const char* invert = "invert";
inline constexpr const char* release = "release";

inline constexpr const char* crossoverHigh = "crossoverhigh";
inline constexpr const char* amountLow = "amountlow";
inline constexpr const char* amountMid = "amountmid";
inline constexpr const char* amountHigh = "amounthigh";
inline constexpr const char* target = "target";
inline constexpr const char* filterMin = "filtermin";
inline constexpr const char* resonance = "resonance";
inline constexpr const char* bassDelay = "bassdelay";

enum Target { targetVolume = 0, targetFilter = 1, targetPan = 2 };
inline constexpr float maxBassDelayMs = 10.0f;
inline constexpr int numBands = 3;

enum Mode { modeSync = 0, modeAudio = 1, modeMidi = 2, modeFollower = 3 };
inline constexpr float maxLookaheadMs = 20.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace psyduck::params
