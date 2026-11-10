#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace psyduck::params
{
inline constexpr const char* rate = "rate";
inline constexpr const char* amount = "amount";
inline constexpr const char* mix = "mix";
inline constexpr const char* phase = "phase";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace psyduck::params
