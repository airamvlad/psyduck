#pragma once

#include "Crossover.h"

#include <cmath>

namespace psyduck
{

/** Measures how the low end of the kick and the bass line up.

    +1: they move together and add up. -1: they cancel each other.
    Around 0: no clear relation. Only frequencies below about 150 Hz count. */
class PhaseMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        kickFilter.set (false, 150.0, sr);
        bassFilter.set (false, 150.0, sr);
        decay = (float) std::exp (-1.0 / (0.3 * sr));
        reset();
    }

    void reset() noexcept
    {
        kickFilter.reset();
        bassFilter.reset();
        kk = bb = kb = 0.0f;
    }

    void process (float kick, float bass) noexcept
    {
        const float k = kickFilter.process (kick);
        const float b = bassFilter.process (bass);
        kk = kk * decay + k * k;
        bb = bb * decay + b * b;
        kb = kb * decay + k * b;
    }

    /** False when the kick and the bass are not sounding together. */
    bool hasSignal() const noexcept { return kk > 1.0e-3f && bb > 1.0e-3f; }

    float getCorrelation() const noexcept
    {
        const float denom = std::sqrt (kk * bb);
        return denom > 1.0e-9f ? std::clamp (kb / denom, -1.0f, 1.0f) : 0.0f;
    }

private:
    Biquad kickFilter, bassFilter;
    float decay = 0.999f;
    float kk = 0.0f, bb = 0.0f, kb = 0.0f;
};

} // namespace psyduck
