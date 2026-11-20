#pragma once

namespace FactoryLiveBudget
{
    constexpr int MaxLivingCreatureCost = 24000;
    constexpr int MaxResidentCreatureCost = 32000;
    constexpr int MaxWorldCreatures = 32000;
    constexpr int MaxPendingCreatures = 2400;
    constexpr double CreatureSpawnInterval = 0.085;
    constexpr int MaxPendingNobelisks = 32;
    constexpr int MaxFlyingNobelisks = 4;

    constexpr bool HasCreatureCapacity(int LivingCost, int ResidentCost, int NextCost)
    {
        return LivingCost + NextCost <= MaxLivingCreatureCost &&
            ResidentCost + NextCost <= MaxResidentCreatureCost;
    }

    struct FRespawnGuard
    {
        bool Observe(double Now, bool bTransitioning)
        {
            if (!bTransitioning) return false;
            const bool bStarted = !IsHolding(Now);
            HoldUntil = Now + 5.0;
            return bStarted;
        }

        bool IsHolding(double Now) const { return Now < HoldUntil; }

    private:
        double HoldUntil = 0.0;
    };

    // Frame and work measurements are monotonic real time, independent of simulation speed.
    struct FFrameBudget
    {
        void Observe(double Now)
        {
            if (LastFrame > 0.0)
            {
                const double Elapsed = Now - LastFrame;
                const double Sample = Elapsed < 0.25 ? Elapsed : 0.25;
                MeanFrame += (Sample - MeanFrame) * 0.08;
                if (Elapsed >= 0.085 || MeanFrame >= 0.047) HoldUntil = Now + 2.0;
                if (Elapsed >= 0.125) SpawnHoldUntil = Now + 0.5;
            }
            LastFrame = Now;
        }

        bool IsPaused(double Now) const { return Now < HoldUntil; }
        bool CanSpawn(double Now) const { return Now >= SpawnHoldUntil; }

        void ReportWork(double Now, double Elapsed)
        {
            if (Elapsed >= 0.018)
            {
                HoldUntil = Now + 2.0;
                const double Pause = Now + 0.20;
                if (SpawnHoldUntil < Pause) SpawnHoldUntil = Pause;
            }
        }

        double SpawnDelay(double Interval) const
        {
            const double Scale = MeanFrame / 0.025;
            if (Scale >= 3.0) return Interval * 3.0;
            return Scale > 1.0 ? Interval * Scale : Interval;
        }

        static double PlacementRetryDelay(int ConsecutiveFailures)
        {
            if (ConsecutiveFailures < 5) return 0.18;
            if (ConsecutiveFailures < 11) return 0.45;
            return 0.85;
        }

    private:
        double LastFrame = 0.0;
        double MeanFrame = 1.0 / 60.0;
        double HoldUntil = 0.0;
        double SpawnHoldUntil = 0.0;
    };
}
