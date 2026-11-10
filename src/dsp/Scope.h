#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>

namespace psyduck
{

/** Peak levels of the kick (sidechain) and the bass (output) laid out along
    the ducking cycle, so the UI can draw both under the curve.
    The audio thread writes, the UI thread reads. */
class Scope
{
public:
    static constexpr int numBins = 256;

    Scope()
    {
        for (auto& v : kick) v.store (0.0f);
        for (auto& v : bass) v.store (0.0f);
    }

    /** Audio thread. phase: position in the cycle, 0..1. */
    void write (float phase, float kickLevel, float bassLevel) noexcept
    {
        const int bin = std::clamp ((int) (phase * (float) numBins), 0, numBins - 1);

        if (bin != currentBin)
        {
            currentBin = bin;
            kickPeak = 0.0f;
            bassPeak = 0.0f;
        }

        kickPeak = std::max (kickPeak, kickLevel);
        bassPeak = std::max (bassPeak, bassLevel);
        kick[(std::size_t) bin].store (kickPeak, std::memory_order_relaxed);
        bass[(std::size_t) bin].store (bassPeak, std::memory_order_relaxed);
    }

    float getKick (int bin) const noexcept { return kick[(std::size_t) bin].load (std::memory_order_relaxed); }
    float getBass (int bin) const noexcept { return bass[(std::size_t) bin].load (std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>, numBins> kick;
    std::array<std::atomic<float>, numBins> bass;

    int currentBin = -1;
    float kickPeak = 0.0f;
    float bassPeak = 0.0f;
};

} // namespace psyduck
