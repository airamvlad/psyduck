#pragma once

#include <algorithm>
#include <cmath>

namespace psyduck
{

/** Resonant low-pass whose cutoff can be moved every few samples without
    clicks (state variable filter). Used when the curve drives a filter
    instead of the volume. */
class ShaperFilter
{
public:
    void reset() noexcept { s1 = s2 = 0.0f; }

    /** resonance: 0 (none) .. 1 (strong). */
    void set (double cutoffHz, double resonance, double sampleRate) noexcept
    {
        const double fc = std::clamp (cutoffHz, 20.0, sampleRate * 0.45);
        const double g = std::tan (3.14159265358979323846 * fc / sampleRate);
        const double k = 2.0 - 1.9 * std::clamp (resonance, 0.0, 1.0);

        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) (g * a1);
        a3 = (float) (g * a2);
    }

    float process (float x) noexcept
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return v2;
    }

    /** Curve value 0..1 to cutoff: 0 gives lowestHz, 1 gives fully open (20 kHz). */
    static double cutoffFor (float value, double lowestHz) noexcept
    {
        const double low = std::clamp (lowestHz, 20.0, 20000.0);
        return low * std::pow (20000.0 / low, (double) std::clamp (value, 0.0f, 1.0f));
    }

private:
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;
};

} // namespace psyduck
