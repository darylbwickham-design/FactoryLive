#pragma once

#include "CoreMinimal.h"
#include "FGInventoryComponent.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "FactoryLiveBackpack.generated.h"

class AFGCharacterPlayer;
class UWorld;

USTRUCT()
struct FFactoryLiveBackpackSnapshot
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    FString Owner;

    UPROPERTY(SaveGame)
    TArray<FInventoryStack> Slots;

    bool Matches(const FString& PlayerKey) const { return !Owner.IsEmpty() && !Slots.IsEmpty() && Owner == PlayerKey; }
};

struct FFactoryLiveBackpackResult
{
    const TCHAR* Code;
    int32 Stacks = 0;
};

UCLASS()
class AFactoryLiveBackpack final : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    AFactoryLiveBackpack();
    static AFactoryLiveBackpack* Get(UWorld* World);
    static FString GetPlayerKey(const AFGCharacterPlayer& Player);

    bool HasSnapshot(const FString& PlayerKey) const { return Snapshot.Matches(PlayerKey); }
    bool HasRecovery(const FString& PlayerKey) const { return Recovery.Matches(PlayerKey); }
    FFactoryLiveBackpackResult Execute(AFGCharacterPlayer& Player, const FString& Action);

    bool NeedTransform_Implementation() override { return false; }
    bool ShouldSave_Implementation() const override { return !Snapshot.Slots.IsEmpty() || !Recovery.Slots.IsEmpty(); }

private:
    FFactoryLiveBackpackResult Replace(UFGInventoryComponent& Inventory, FFactoryLiveBackpackSnapshot Before,
        const FFactoryLiveBackpackSnapshot& Target, const TCHAR* SuccessCode);

    UPROPERTY(SaveGame)
    FFactoryLiveBackpackSnapshot Snapshot;

    UPROPERTY(SaveGame)
    FFactoryLiveBackpackSnapshot Recovery;
};
