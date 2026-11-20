#pragma once

namespace FactoryLiveVisibility
{
    constexpr float DefaultScale = 1.5f;
    constexpr float DefaultStingerMinimumMeters = 200.0f;

    constexpr float ExtendedDistance(float Current, float Scale, float Minimum)
    {
        if (Current <= 0.0f || Scale <= 1.0f) return Current;
        const float Scaled = Current * Scale;
        return Scaled >= Minimum ? Scaled : Minimum;
    }
}
