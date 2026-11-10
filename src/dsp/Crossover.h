#pragma once

#include <algorithm>
#include <cmath>

namespace psyduck
{

/** Second-order filter section (transposed direct form II). */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Butterworth low-pass or high-pass. */
    void set (bool highpass, double frequency, double sampleRate) noexcept
    {
        const double pi = 3.14159265358979323846;
        const double fc = std::clamp (frequency, 10.0, sampleRate * 0.45);
        const double w = 2.0 * pi * fc / sampleRate;
        const double cosw = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;

        const double edge = highpass ? (1.0 + cosw) : (1.0 - cosw);
        b0 = (float) (edge * 0.5 / a0);
        b1 = (float) ((highpass ? -edge : edge) / a0);
        b2 = b0;
        a1 = (float) (-2.0 * cosw / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }
};

/** Linkwitz-Riley 24 dB/oct split into a low and a high band.
    The two bands add back up to the full signal (flat level). */
class BandSplit
{
public:
    void reset() noexcept
    {
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
    }

    void setFrequency (double frequency, double sampleRate) noexcept
    {
        for (auto& f : lp) f.set (false, frequency, sampleRate);
        for (auto& f : hp) f.set (true, frequency, sampleRate);
    }

    void process (float x, float& low, float& high) noexcept
    {
        low = lp[1].process (lp[0].process (x));
        high = hp[1].process (hp[0].process (x));
    }

private:
    Biquad lp[2], hp[2];
};

/** Splits into low, mid and high. The three bands add back up to the full
    signal with a flat level. */
class ThreeBandSplit
{
public:
    void reset() noexcept
    {
        lowSplit.reset();
        highSplit.reset();
        lowAlign.reset();
    }

    void setFrequencies (double lowHz, double highHz, double sampleRate) noexcept
    {
        lowSplit.setFrequency (lowHz, sampleRate);
        highSplit.setFrequency (highHz, sampleRate);
        lowAlign.setFrequency (highHz, sampleRate);
    }

    void process (float x, float& low, float& mid, float& high) noexcept
    {
        float lowRaw = 0.0f, rest = 0.0f;
        lowSplit.process (x, lowRaw, rest);
        highSplit.process (rest, mid, high);

        // Give the low band the same phase shift the upper split adds,
        // otherwise the bands would not add up flat.
        float a = 0.0f, b = 0.0f;
        lowAlign.process (lowRaw, a, b);
        low = a + b;
    }

private:
    BandSplit lowSplit, highSplit, lowAlign;
};

} // namespace psyduck
