#pragma once

#include "Curve.h"

#include <algorithm>
#include <vector>

namespace psyduck
{

/** Builds a starting curve from the kick levels captured along the cycle:
    silent while the body of the kick plays, back to full level when its tail
    has faded. Returns false when there is no usable kick signal.

    kick: peak level per position in the cycle. Not for the audio thread. */
inline bool suggestCurve (const std::vector<float>& kick, Curve& result)
{
    const int n = (int) kick.size();
    if (n < 16)
        return false;

    const float peak = *std::max_element (kick.begin(), kick.end());
    if (peak < 0.01f)
        return false;

    // Where the kick starts: the first position that reaches half of the peak.
    int onset = 0;
    while (onset < n && kick[(std::size_t) onset] < 0.5f * peak)
        ++onset;

    // Smooth over the gaps between the kick's own oscillations.
    std::vector<float> envelope ((std::size_t) n, 0.0f);
    float held = 0.0f;
    for (int i = onset; i < n; ++i)
    {
        held = std::max (kick[(std::size_t) i] / peak, held * 0.97f);
        envelope[(std::size_t) i] = held;
    }

    // Body: until the level falls under 50%. Tail: until it falls under 10%.
    int body = onset, tail = onset;
    for (int i = onset; i < n; ++i)
    {
        if (envelope[(std::size_t) i] >= 0.5f) body = i;
        if (envelope[(std::size_t) i] >= 0.1f) tail = i;
    }

    const float step = 1.0f / (float) n;
    const float start = (float) onset * step;
    float holdEnd = std::clamp ((float) (body + 1) * step, start + 0.02f, 0.80f);
    float riseEnd = std::clamp ((float) (tail + 1) * step, holdEnd + 0.05f, 0.92f);

    Curve c;
    c.clear();

    if (start > 0.04f)
    {
        // Kick is not at the start of the cycle: stay open until just before it.
        c.addPoint ({ 0.0f, 1.0f, 0.0f });
        c.addPoint ({ start - 0.03f, 1.0f, 0.0f });
        c.addPoint ({ start, 0.0f, 0.0f });
        c.addPoint ({ holdEnd, 0.0f, 0.25f });
        c.addPoint ({ riseEnd, 1.0f, 0.0f });
        c.addPoint ({ 1.0f, 1.0f, 0.0f });
    }
    else
    {
        c.addPoint ({ 0.0f, 0.0f, 0.0f });
        c.addPoint ({ holdEnd, 0.0f, 0.25f });
        c.addPoint ({ riseEnd, 1.0f, 0.0f });
        c.addPoint ({ 0.96f, 1.0f, 0.0f });
        c.addPoint ({ 1.0f, 0.0f, 0.0f });
    }

    result = c;
    return true;
}

} // namespace psyduck
