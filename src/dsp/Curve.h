#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace psyduck
{

/** One control point of the ducking curve.
    x: position in the cycle, 0..1
    y: gain at that position, 0 (silent) .. 1 (full level)
    curve: bend of the segment that starts at this point, -1..1 (0 = straight line) */
struct CurvePoint
{
    float x = 0.0f;
    float y = 1.0f;
    float curve = 0.0f;
};

/** A ducking curve with a fixed maximum number of points.
    Nothing here allocates, so it is safe to evaluate on the audio thread. */
class Curve
{
public:
    static constexpr std::size_t maxPoints = 32;

    Curve() { setToDefaultDuck(); }

    /** Classic kick duck: silent while the kick hits, then the bass swells back.
        The short fall at the very end avoids a hard cut (click) when the cycle restarts. */
    void setToDefaultDuck() noexcept
    {
        numPoints = 0;
        addPoint ({ 0.00f, 0.0f, 0.0f });
        addPoint ({ 0.08f, 0.0f, 0.35f });
        addPoint ({ 0.60f, 1.0f, 0.0f });
        addPoint ({ 0.96f, 1.0f, 0.0f });
        addPoint ({ 1.00f, 0.0f, 0.0f });
    }

    void clear() noexcept { numPoints = 0; }

    /** Adds a point and keeps the points sorted by x.
        Returns the index of the new point, or -1 when the curve is full. */
    int addPoint (CurvePoint p) noexcept
    {
        if (numPoints >= maxPoints)
            return -1;

        p.x = std::clamp (p.x, 0.0f, 1.0f);
        p.y = std::clamp (p.y, 0.0f, 1.0f);
        p.curve = std::clamp (p.curve, -1.0f, 1.0f);

        std::size_t i = numPoints;
        while (i > 0 && points[i - 1].x > p.x)
        {
            points[i] = points[i - 1];
            --i;
        }
        points[i] = p;
        ++numPoints;
        return static_cast<int> (i);
    }

    /** Removes a point. The first and last points always stay. */
    bool removePoint (std::size_t i) noexcept
    {
        if (i == 0 || i + 1 >= numPoints)
            return false;

        for (std::size_t k = i; k + 1 < numPoints; ++k)
            points[k] = points[k + 1];
        --numPoints;
        return true;
    }

    /** Moves a point. It cannot pass its neighbours, and the first and last
        points stay at the edges of the cycle. */
    void movePoint (std::size_t i, float x, float y) noexcept
    {
        if (i >= numPoints)
            return;

        if (i == 0)
            x = points[0].x;
        else if (i + 1 == numPoints)
            x = points[i].x;
        else
            x = std::clamp (x, points[i - 1].x, points[i + 1].x);

        points[i].x = x;
        points[i].y = std::clamp (y, 0.0f, 1.0f);
    }

    /** Sets the bend (-1..1) of the segment that starts at point i. */
    void setSegmentCurve (std::size_t i, float curve) noexcept
    {
        if (i < numPoints)
            points[i].curve = std::clamp (curve, -1.0f, 1.0f);
    }

    std::size_t size() const noexcept { return numPoints; }
    const CurvePoint& operator[] (std::size_t i) const noexcept { return points[i]; }

    /** Gain (0..1) at a position in the cycle (0..1). */
    float evaluate (float phase) const noexcept
    {
        if (numPoints == 0)
            return 1.0f;

        phase = std::clamp (phase, 0.0f, 1.0f);

        if (phase <= points[0].x)
            return points[0].y;

        for (std::size_t i = 1; i < numPoints; ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];

            if (phase <= b.x)
            {
                const float span = b.x - a.x;
                if (span <= 1.0e-9f)
                    return b.y;

                const float t = shape ((phase - a.x) / span, a.curve);
                return a.y + (b.y - a.y) * t;
            }
        }

        return points[numPoints - 1].y;
    }

private:
    /** Bends t (0..1) with a power curve. Positive values rise fast then flatten. */
    static float shape (float t, float curve) noexcept
    {
        if (std::abs (curve) < 1.0e-4f)
            return t;

        const float exponent = std::pow (4.0f, std::abs (curve));
        return curve > 0.0f ? 1.0f - std::pow (1.0f - t, exponent)
                            : std::pow (t, exponent);
    }

    std::array<CurvePoint, maxPoints> points {};
    std::size_t numPoints = 0;
};

} // namespace psyduck
