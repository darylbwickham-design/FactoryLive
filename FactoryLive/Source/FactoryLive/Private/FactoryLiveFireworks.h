#pragma once

#include <cmath>

namespace FactoryLiveFireworks
{
    constexpr int DropsPerStar = 25;
    constexpr int DropCount = 250;
    constexpr int MaxInFlight = 16;
    constexpr double LaunchInterval = 0.026;
    constexpr double ImpactInterval = 0.045;
    constexpr double StarInterval = 0.42;

    struct FStarOffset
    {
        double X;
        double Y;
    };

    inline FStarOffset StarOffset(int Launched, double Rotation)
    {
        constexpr double TipStep = 1.2566370614359172;
        constexpr int SamplesPerTip = DropsPerStar / 5;
        const int Star = Launched / DropsPerStar;
        const int Sample = Launched % DropsPerStar;
        const int Point = Sample % SamplesPerTip;
        const double Angle = Rotation + Star * 0.31 + (Sample / SamplesPerTip) * TipStep;
        const double OuterRadius = 1450.0 + (Star % 3) * 350.0;
        const double InnerRadius = OuterRadius * 0.43;
        // Walk from the previous valley to the tip, then toward the next valley.
        const double ValleyAngle = Angle + (Point < 2 ? -0.5 : 0.5) * TipStep;
        const double TipWeight = Point < 2 ? Point / 2.0 : (SamplesPerTip - Point) / 3.0;
        return {
            TipWeight * OuterRadius * std::cos(Angle) + (1.0 - TipWeight) * InnerRadius * std::cos(ValleyAngle),
            TipWeight * OuterRadius * std::sin(Angle) + (1.0 - TipWeight) * InnerRadius * std::sin(ValleyAngle)
        };
    }

    constexpr double DelayAfterLaunch(int Launched)
    {
        return Launched % DropsPerStar == 0 ? StarInterval : LaunchInterval;
    }
}
