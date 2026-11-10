#pragma once

#include "Curve.h"

#include <array>
#include <initializer_list>

namespace psyduck
{

inline constexpr int numPresets = 6;

inline constexpr std::array<const char*, numPresets> presetNames {
    "Psy Duck", "Tight", "Long Pump", "Soft", "Gate", "Flat (no ducking)"
};

/** Factory curves. Index must be 0..numPresets-1. */
inline Curve makePreset (int index) noexcept
{
    Curve c;
    c.clear();

    const auto set = [&c] (std::initializer_list<CurvePoint> pts)
    {
        for (const auto& p : pts)
            c.addPoint (p);
    };

    switch (index)
    {
        case 1:  set ({ { 0.0f, 0.0f, 0.0f }, { 0.04f, 0.0f, 0.2f }, { 0.30f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }); break;
        case 2:  set ({ { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.0f, -0.3f }, { 0.90f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }); break;
        case 3:  set ({ { 0.0f, 0.3f, 0.4f }, { 0.50f, 1.0f, 0.0f }, { 0.94f, 1.0f, 0.0f }, { 1.0f, 0.3f, 0.0f } }); break;
        case 4:  set ({ { 0.0f, 0.0f, 0.0f }, { 0.48f, 0.0f, 0.0f }, { 0.52f, 1.0f, 0.0f }, { 0.97f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }); break;
        case 5:  set ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }); break;
        default: c.setToDefaultDuck(); break;
    }

    return c;
}

} // namespace psyduck
