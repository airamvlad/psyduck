#pragma once

#include <array>
#include <cmath>

namespace psyduck
{

/** Tempo-synced cycle lengths, in quarter notes. Order must match the Rate parameter. */
inline constexpr std::array<double, 9> rateBeats {
    4.0,        // 1/1
    2.0,        // 1/2
    1.0,        // 1/4
    0.5,        // 1/8
    0.25,       // 1/16
    2.0 / 3.0,  // 1/4T
    1.0 / 3.0,  // 1/8T
    1.0 / 6.0,  // 1/16T
    0.75        // 1/8D
};

inline constexpr std::array<const char*, 9> rateNames {
    "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/16T", "1/8D"
};

inline constexpr int defaultRateIndex = 2; // 1/4

/** Tracks the position inside the ducking cycle, sample by sample.

    While the host plays, the phase is locked to the song position at the start
    of every block, so the curve never drifts. While stopped it free-runs at the
    host tempo so the effect can still be heard. */
class SyncEngine
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        phase = 0.0;
    }

    /** Call once per block.
        ppqPosition: song position in quarter notes at the first sample of the block. */
    void beginBlock (double bpm, double beatsPerCycle, bool isPlaying, double ppqPosition) noexcept
    {
        if (bpm <= 0.0)
            bpm = 120.0;
        if (beatsPerCycle <= 0.0)
            beatsPerCycle = 1.0;

        increment = (bpm / 60.0) / sampleRate / beatsPerCycle;

        if (isPlaying)
            phase = wrap (ppqPosition / beatsPerCycle);
    }

    /** Returns the phase (0..1) of the current sample, shifted by offset (0..1), then advances. */
    float next (double offset) noexcept
    {
        const double out = wrap (phase + offset);
        phase += increment;
        if (phase >= 1.0)
            phase -= std::floor (phase);
        return static_cast<float> (out);
    }

    double getPhase() const noexcept { return phase; }

    /** How far the phase moves per sample. */
    double getIncrement() const noexcept { return increment; }

private:
    static double wrap (double v) noexcept { return v - std::floor (v); }

    double sampleRate = 44100.0;
    double phase = 0.0;
    double increment = 0.0;
};

} // namespace psyduck
