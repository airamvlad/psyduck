#pragma once

#include <algorithm>
#include <cmath>

namespace psyduck
{

/** Turns a curve value into the final gain and smooths it to avoid clicks. */
class GainProcessor
{
public:
    void prepare (double sampleRate, double smoothingMs = 1.0) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        coeff = static_cast<float> (1.0 - std::exp (-1.0 / (sr * smoothingMs * 0.001)));
        current = 1.0f;
    }

    /** curveValue: 0..1 from the curve. amount, mix: 0..1. Returns the gain for this sample. */
    float next (float curveValue, float amount, float mix) noexcept
    {
        const float target = targetGain (curveValue, amount, mix);
        current += (target - current) * coeff;
        return current;
    }

    /** amount sets how deep the duck goes, mix blends the ducked signal with the dry one. */
    static float targetGain (float curveValue, float amount, float mix) noexcept
    {
        const float ducked = 1.0f - std::clamp (amount, 0.0f, 1.0f) * (1.0f - std::clamp (curveValue, 0.0f, 1.0f));
        return 1.0f - std::clamp (mix, 0.0f, 1.0f) * (1.0f - ducked);
    }

private:
    float coeff = 1.0f;
    float current = 1.0f;
};

} // namespace psyduck
