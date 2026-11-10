#pragma once

#include "Curve.h"

#include <algorithm>

namespace psyduck
{

/** Plays the curve once per trigger (AUDIO and MIDI modes).

    While idle the output is 1 (no ducking). With lookahead, a trigger first
    glides from the current value down to the start of the curve, so the duck
    is already in place when the delayed audio arrives and nothing clicks. */
class OneShot
{
public:
    void reset() noexcept
    {
        active = false;
        phase = 0.0;
        countdown = -1;
        last = 1.0f;
    }

    /** lengthSamples: how long one pass over the curve takes. */
    void setLength (double lengthSamples) noexcept
    {
        increment = lengthSamples > 1.0 ? 1.0 / lengthSamples : 1.0;
    }

    /** The curve starts lookaheadSamples after the trigger. */
    void trigger (int lookaheadSamples) noexcept
    {
        glideLength = lookaheadSamples > 0 ? lookaheadSamples : 0;
        countdown = glideLength;
        glideFrom = last;
        active = false;
    }

    float next (const Curve& curve) noexcept
    {
        if (countdown > 0)
        {
            // Reach the bottom halfway through the lookahead and hold it there, so
            // the gain smoothing has settled by the time the kick arrives.
            const float elapsed = (float) (glideLength - countdown + 1);
            const float t = std::min (1.0f, 2.0f * elapsed / (float) glideLength);
            --countdown;
            last = glideFrom + (curve.evaluate (0.0f) - glideFrom) * t;
            return last;
        }

        if (countdown == 0)
        {
            countdown = -1;
            active = true;
            phase = 0.0;
        }

        if (active)
        {
            last = curve.evaluate ((float) phase);
            phase += increment;
            if (phase >= 1.0)
                active = false;
        }
        else
        {
            last = 1.0f;
        }

        return last;
    }

    bool isActive() const noexcept { return active; }

    /** True while waiting for the next trigger (no glide, no curve playing). */
    bool isIdle() const noexcept { return ! active && countdown < 0; }

    /** Position in the curve (0..1), or -1 when no shot is playing. */
    float getPhase() const noexcept { return active ? (float) phase : -1.0f; }

private:
    bool active = false;
    double phase = 0.0;
    double increment = 1.0;
    int countdown = -1;
    int glideLength = 0;
    float glideFrom = 1.0f;
    float last = 1.0f;
};

} // namespace psyduck
