#include "../FactoryLive/Source/FactoryLive/Private/FactoryLiveBackpackTransaction.h"
#include <array>

#define CHECK(Condition) do { if (!(Condition)) return __LINE__; } while (false)

int main()
{
    const std::array<int, 3> Before{1, 2, 3};
    const std::array<int, 3> Snapshot{4, 0, 6};
    auto Inventory = Before;
    auto Result = ReplaceBackpackSlots(Before, Snapshot, [&](const auto& Slots) {
        Inventory = Slots;
        return true;
    });
    CHECK(Result == EFactoryLiveBackpackWrite::Applied && Inventory == Snapshot);

    int Writes = 0;
    Result = ReplaceBackpackSlots(Before, Snapshot, [&](const auto& Slots) {
        Inventory[0] = Slots[0];
        if (++Writes == 1) return false;
        Inventory = Slots;
        return true;
    });
    CHECK(Result == EFactoryLiveBackpackWrite::RolledBack && Inventory == Before && Writes == 2);

    Writes = 0;
    Result = ReplaceBackpackSlots(Before, Snapshot, [&](const auto&) {
        ++Writes;
        return false;
    });
    CHECK(Result == EFactoryLiveBackpackWrite::RecoveryRequired && Writes == 2);
    return 0;
}
