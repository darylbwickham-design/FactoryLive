#include "../FactoryLive/Source/FactoryLive/Private/FactoryLiveBudget.h"
#include "../FactoryLive/Source/FactoryLive/Private/FactoryLiveFireworks.h"
#include "../FactoryLive/Source/FactoryLive/Private/FactoryLiveVisibility.h"

#define CHECK(Condition) do { if (!(Condition)) return __LINE__; } while (false)

int main()
{
    static_assert(FactoryLiveVisibility::ExtendedDistance(0.0f, 1.5f, 20000.0f) == 0.0f);
    static_assert(FactoryLiveVisibility::ExtendedDistance(6000.0f, 1.0f, 20000.0f) == 6000.0f);
    static_assert(FactoryLiveVisibility::ExtendedDistance(6000.0f, 1.5f, 0.0f) == 9000.0f);
    static_assert(FactoryLiveVisibility::ExtendedDistance(6000.0f, 1.5f, 20000.0f) == 20000.0f);
    static_assert(FactoryLiveVisibility::ExtendedDistance(16000.0f, 1.5f, 20000.0f) == 24000.0f);
    static_assert(FactoryLiveFireworks::DropCount == 10 * FactoryLiveFireworks::DropsPerStar);
    static_assert(FactoryLiveFireworks::DelayAfterLaunch(24) == 0.026);
    static_assert(FactoryLiveFireworks::ImpactInterval > FactoryLiveFireworks::LaunchInterval);
    static_assert(FactoryLiveFireworks::MaxInFlight == 16);
    static_assert(FactoryLiveFireworks::DelayAfterLaunch(25) > 0.40);
    const auto FirstTip = FactoryLiveFireworks::StarOffset(2, 0.0);
    const auto FirstValley = FactoryLiveFireworks::StarOffset(0, 0.0);
    const auto NextStarTip = FactoryLiveFireworks::StarOffset(27, 0.0);
    CHECK(FirstTip.X == 1450.0 && FirstTip.Y == 0.0);
    CHECK(FirstValley.X > 0.0 && FirstValley.X < FirstTip.X && FirstValley.Y < 0.0);
    CHECK(NextStarTip.X > FirstTip.X && NextStarTip.Y > 0.0);

    FactoryLiveBudget::FFrameBudget Budget;
    double Now = 1.0;
    const auto Frames = [&](int Count, double Interval) {
        for (int Index = 0; Index < Count; ++Index)
        {
            Now += Interval;
            Budget.Observe(Now);
        }
    };

    Frames(120, 1.0 / 30.0);
    CHECK(!Budget.IsPaused(Now));
    static_assert(FactoryLiveBudget::CreatureSpawnInterval == 0.085);
    CHECK(Budget.SpawnDelay(0.085) > 0.085 && Budget.SpawnDelay(0.085) < 0.12);
    CHECK(FactoryLiveBudget::HasCreatureCapacity(23988, 31988, 12));
    CHECK(!FactoryLiveBudget::HasCreatureCapacity(23989, 31988, 12));
    CHECK(!FactoryLiveBudget::HasCreatureCapacity(23988, 31989, 12));
    CHECK(Budget.PlacementRetryDelay(1) < Budget.PlacementRetryDelay(6));
    CHECK(Budget.PlacementRetryDelay(6) < Budget.PlacementRetryDelay(15));

    Frames(80, 0.040);
    CHECK(!Budget.IsPaused(Now));

    Frames(80, 0.055);
    CHECK(Budget.IsPaused(Now));
    Frames(200, 1.0 / 60.0);
    CHECK(!Budget.IsPaused(Now));
    CHECK(Budget.SpawnDelay(0.085) == 0.085);

    Frames(1, 0.1);
    CHECK(Budget.IsPaused(Now));
    Frames(40, 1.0 / 60.0);
    CHECK(Budget.IsPaused(Now));
    Frames(160, 1.0 / 60.0);
    CHECK(!Budget.IsPaused(Now));

    Budget.ReportWork(Now, 0.020);
    CHECK(Budget.IsPaused(Now));
    CHECK(!Budget.CanSpawn(Now));
    CHECK(Budget.CanSpawn(Now + 0.21));

    FactoryLiveBudget::FRespawnGuard Respawn;
    CHECK(Respawn.Observe(10.0, true));
    CHECK(!Respawn.Observe(11.0, true));
    CHECK(!Respawn.Observe(12.0, false));
    CHECK(Respawn.IsHolding(15.99));
    CHECK(!Respawn.IsHolding(16.0));
    return 0;
}
