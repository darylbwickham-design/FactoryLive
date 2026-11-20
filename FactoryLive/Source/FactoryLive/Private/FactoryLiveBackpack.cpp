#include "FactoryLiveBackpack.h"
#include "FactoryLiveBackpackTransaction.h"

#include "FGCharacterPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Subsystem/SubsystemActorManager.h"

namespace
{
    constexpr int32 MaxBackpackSlots = 512;

    bool IsStackValid(const FInventoryStack& Stack)
    {
        return Stack.NumItems == 0 || (Stack.HasItems() && !Stack.Item.LegacyItemStateActor);
    }

    bool SlotAccepts(const UFGInventoryComponent& Inventory, const FInventoryStack& Stack, int32 Slot)
    {
        return IsStackValid(Stack) && (!Stack.HasItems() ||
            (Inventory.IsItemAllowed(Stack.Item.GetItemClass(), Slot) &&
                Stack.NumItems <= Inventory.GetSlotSizeForItem(Slot, Stack.Item.GetItemClass(), &Stack.Item)));
    }

    int32 CountStacks(const TArray<FInventoryStack>& Slots)
    {
        int32 Count = 0;
        for (const FInventoryStack& Stack : Slots)
        {
            if (Stack.HasItems()) ++Count;
        }
        return Count;
    }

    bool Fits(const UFGInventoryComponent& Inventory, const TArray<FInventoryStack>& Slots)
    {
        if (Slots.Num() > Inventory.GetSizeLinear()) return false;
        for (int32 Index = 0; Index < Slots.Num(); ++Index)
        {
            if (!SlotAccepts(Inventory, Slots[Index], Index)) return false;
        }
        return true;
    }

    bool WriteAndVerify(UFGInventoryComponent& Inventory, const TArray<FInventoryStack>& Slots)
    {
        Inventory.Empty();
        for (int32 Index = 0; Index < Slots.Num(); ++Index)
        {
            const FInventoryStack& Stack = Slots[Index];
            if (Stack.HasItems() && Inventory.AddStackToIndex(Index, Stack) != Stack.NumItems) return false;
        }
        TArray<FInventoryStack> Actual;
        Inventory.GetInventoryStacks(Actual, true);
        if (Actual.Num() != Inventory.GetSizeLinear()) return false;
        for (int32 Index = 0; Index < Actual.Num(); ++Index)
        {
            const FInventoryStack& Stack = Actual[Index];
            if (!Slots.IsValidIndex(Index) || !Slots[Index].HasItems())
            {
                if (Stack.HasItems()) return false;
                continue;
            }
            const FInventoryStack& Expected = Slots[Index];
            if (Stack.NumItems != Expected.NumItems || Stack.Item.GetItemClass() != Expected.Item.GetItemClass() ||
                !Stack.Item.GetItemState().Identical(Expected.Item.GetItemState())) return false;
        }
        return true;
    }

}

AFactoryLiveBackpack::AFactoryLiveBackpack()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}

AFactoryLiveBackpack* AFactoryLiveBackpack::Get(UWorld* World)
{
    auto* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
    return Manager ? Manager->GetSubsystemActor<AFactoryLiveBackpack>() : nullptr;
}

FString AFactoryLiveBackpack::GetPlayerKey(const AFGCharacterPlayer& Player)
{
    const APlayerController* Controller = Cast<APlayerController>(Player.GetController());
    if (!Controller || !Controller->IsLocalController()) return {};
    const APlayerState* State = Controller->GetPlayerState<APlayerState>();
    if (State && State->GetUniqueId().IsValid()) return State->GetUniqueId().ToString();
    return TEXT("local-host");
}

FFactoryLiveBackpackResult AFactoryLiveBackpack::Execute(AFGCharacterPlayer& Player, const FString& Action)
{
    UFGInventoryComponent* Inventory = Player.GetInventory();
    const FString PlayerKey = GetPlayerKey(Player);
    if (!Inventory || PlayerKey.IsEmpty() || !Inventory->HasAuthority()) return {TEXT("inventory_unavailable")};
    if (Inventory->IsLocked()) return {TEXT("inventory_locked")};
    const int32 Size = Inventory->GetSizeLinear();
    if (Size < 1 || Size > MaxBackpackSlots) return {TEXT("inventory_size_unsupported")};

    FFactoryLiveBackpackSnapshot Before;
    Before.Owner = PlayerKey;
    Inventory->GetInventoryStacks(Before.Slots, true);
    if (Before.Slots.Num() != Size || Before.Slots.ContainsByPredicate([](const FInventoryStack& Stack) {
        return !IsStackValid(Stack);
    })) return {TEXT("inventory_unavailable")};

    if (Action == TEXT("snapshot"))
    {
        Snapshot = MoveTemp(Before);
        return {TEXT("saved"), CountStacks(Snapshot.Slots)};
    }
    if (Action == TEXT("restore")) return Replace(*Inventory, MoveTemp(Before), Snapshot, TEXT("restored"));
    if (Action == TEXT("undo")) return Replace(*Inventory, MoveTemp(Before), Recovery, TEXT("undone"));
    return {TEXT("unknown_inventory_action")};
}

FFactoryLiveBackpackResult AFactoryLiveBackpack::Replace(UFGInventoryComponent& Inventory,
    FFactoryLiveBackpackSnapshot Before, const FFactoryLiveBackpackSnapshot& Target, const TCHAR* SuccessCode)
{
    if (!Target.Matches(Before.Owner)) return {TEXT("snapshot_unavailable")};
    if (!Fits(Inventory, Target.Slots) || !Fits(Inventory, Before.Slots)) return {TEXT("inventory_incompatible")};
    const int32 Stacks = CountStacks(Target.Slots);
    const EFactoryLiveBackpackWrite Result = ReplaceBackpackSlots(Before.Slots, Target.Slots,
        [&](const TArray<FInventoryStack>& Slots) { return WriteAndVerify(Inventory, Slots); });
    if (Result == EFactoryLiveBackpackWrite::RolledBack) return {TEXT("restore_rolled_back")};
    Recovery = MoveTemp(Before);
    if (Result == EFactoryLiveBackpackWrite::Applied)
    {
        return {SuccessCode, Stacks};
    }

    // Keep the pre-change contents available to undo even if another inventory hook blocked both writes.
    UE_LOG(LogTemp, Error, TEXT("Factory Live inventory restore and rollback failed. Recovery was retained."));
    return {TEXT("recovery_required"), CountStacks(Recovery.Slots)};
}
