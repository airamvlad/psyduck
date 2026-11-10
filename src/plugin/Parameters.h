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

enum Mode { modeSync = 0, modeAudio = 1, modeMidi = 2, modeFollower = 3 };
inline constexpr float maxLookaheadMs = 20.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace psyduck::params
