#pragma once

#include <algorithm>
#include <cmath>

namespace psyduck
{

/** Follower mode: the bass is pushed down by the level of the kick itself,
    like a classic sidechain compressor, instead of following the drawn curve. */
class EnvelopeFollower
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        attack = (float) (1.0 - std::exp (-1.0 / (0.0005 * sampleRate)));
        setRelease (120.0f);
        envelope = 0.0f;
    }

    void setRelease (float milliseconds) noexcept
    {
        release = (float) std::exp (-1.0 / (std::max (1.0f, milliseconds) * 0.001 * sampleRate));
    }

    void reset() noexcept { envelope = 0.0f; }

    /** level: absolute kick sample. fullDuckLevel: kick level that silences the bass.
        Returns the curve value, 1 (no ducking) .. 0 (full ducking). */
    float process (float level, float fullDuckLevel) noexcept
    {
        if (level > envelope)
            envelope += (level - envelope) * attack;
        else
            envelope *= release;

        return 1.0f - std::clamp (envelope / std::max (fullDuckLevel, 1.0e-6f), 0.0f, 1.0f);
    }

private:
    double sampleRate = 44100.0;
    float attack = 1.0f;
    float release = 0.999f;
    float envelope = 0.0f;
};

} // namespace psyduck
