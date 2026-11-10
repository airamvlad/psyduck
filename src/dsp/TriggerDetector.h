#pragma once

#include <algorithm>
#include <cmath>

namespace psyduck
{

/** Detects kick hits on the sidechain signal.

    A trigger fires when the level rises above the threshold. It can fire again
    only after the level has dropped 6 dB below the threshold (hysteresis) and
    the retrigger time has passed, so one kick never fires twice. */
class TriggerDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        release = (float) std::exp (-1.0 / (0.010 * sr));
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
        armed = true;
        sinceTrigger = 1 << 30;
    }

    /** level: absolute sample value. threshold: linear gain. Returns true on a new hit. */
    bool process (float level, float threshold, int retriggerSamples) noexcept
    {
        envelope = std::max (level, envelope * release);

        if (sinceTrigger < (1 << 30))
            ++sinceTrigger;

        if (! armed && envelope < threshold * 0.5f)
            armed = true;

        if (armed && envelope >= threshold && sinceTrigger >= retriggerSamples)
        {
            armed = false;
            sinceTrigger = 0;
            return true;
        }

        return false;
    }

private:
    float release = 0.999f;
    float envelope = 0.0f;
    bool armed = true;
    int sinceTrigger = 1 << 30;
};

} // namespace psyduck
