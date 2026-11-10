#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace psyduck
{

/** The kick (sidechain) and the bass (output) laid out along the ducking
    cycle, so the UI can draw both under the curve. Every position keeps the
    lowest and highest sample seen there, which gives both a waveform view and
    a level view. The audio thread writes, the UI thread reads. */
class Scope
{
public:
    static constexpr int numBins = 512;

    Scope()
    {
        for (auto* band : { &kickMin, &kickMax, &bassMin, &bassMax })
            for (auto& v : *band)
                v.store (0.0f);
    }

    /** Audio thread. phase: position in the cycle, 0..1. Samples are signed. */
    void write (float phase, float kickSample, float bassSample) noexcept
    {
        const int bin = std::clamp ((int) (phase * (float) numBins), 0, numBins - 1);

        if (bin != currentBin)
        {
            currentBin = bin;
            kLo = kHi = kickSample;
            bLo = bHi = bassSample;
        }

        kLo = std::min (kLo, kickSample);
        kHi = std::max (kHi, kickSample);
        bLo = std::min (bLo, bassSample);
        bHi = std::max (bHi, bassSample);

        const auto i = (std::size_t) bin;
        kickMin[i].store (kLo, std::memory_order_relaxed);
        kickMax[i].store (kHi, std::memory_order_relaxed);
        bassMin[i].store (bLo, std::memory_order_relaxed);
        bassMax[i].store (bHi, std::memory_order_relaxed);
    }

    float getKickMin (int bin) const noexcept { return kickMin[(std::size_t) bin].load (std::memory_order_relaxed); }
    float getKickMax (int bin) const noexcept { return kickMax[(std::size_t) bin].load (std::memory_order_relaxed); }
    float getBassMin (int bin) const noexcept { return bassMin[(std::size_t) bin].load (std::memory_order_relaxed); }
    float getBassMax (int bin) const noexcept { return bassMax[(std::size_t) bin].load (std::memory_order_relaxed); }

    /** Peak level at a position, 0..1. */
    float getKick (int bin) const noexcept { return std::max (std::abs (getKickMin (bin)), std::abs (getKickMax (bin))); }
    float getBass (int bin) const noexcept { return std::max (std::abs (getBassMin (bin)), std::abs (getBassMax (bin))); }

private:
    using Bins = std::array<std::atomic<float>, numBins>;
    Bins kickMin, kickMax, bassMin, bassMax;

    int currentBin = -1;
    float kLo = 0.0f, kHi = 0.0f, bLo = 0.0f, bHi = 0.0f;
};

} // namespace psyduck
