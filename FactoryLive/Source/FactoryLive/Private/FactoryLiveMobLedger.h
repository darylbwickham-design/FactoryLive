#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "Module/GameWorldModule.h"
#include "Subsystem/ModSubsystem.h"
#include "UObject/ObjectKey.h"
#include "FactoryLiveMobLedger.generated.h"

class AFGCreature;
class AFGCrabHatcher;

UENUM()
enum class EFactoryLiveMobState : uint8
{
    Alive,
    Killed,
    Missing
};

USTRUCT()
struct FFactoryLiveMobRecord
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    FGuid Id;

    UPROPERTY(SaveGame)
    FString JobId;

    UPROPERTY(SaveGame)
    FName Species;

    UPROPERTY(SaveGame)
    EFactoryLiveMobState State = EFactoryLiveMobState::Alive;

    UPROPERTY(SaveGame)
    TObjectPtr<AFGCreature> Actor = nullptr;

    UPROPERTY(SaveGame)
    TObjectPtr<AFGCrabHatcher> Hatcher = nullptr;

    AActor* GetActor() const;

};

UCLASS()
class AFactoryLiveMobLedger final : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    AFactoryLiveMobLedger();
    static AFactoryLiveMobLedger* Get(UWorld* World);

    bool Track(AFGCreature& Creature, FName Species, const FString& JobId);
    bool Track(AFGCrabHatcher& Hatcher, FName Species, const FString& JobId);
    static int32 GetCreatureCost(FName Species);
    int32 GetLivingCost() const { return LivingCost; }
    int32 GetLivingHatchers() const { return LivingHatchers; }
    int32 GetOccupiedCost();
    int32 GetSpawnedCount() const { return Records.Num(); }
    int32 GetAliveCount() const { return Records.Num() - KilledCount - MissingCount; }
    int32 GetKilledCount() const { return KilledCount; }
    int32 GetMissingCount() const { return MissingCount; }
    void ApplyPendingVisibility();
    void PrintHistory(FOutputDevice& Output, int32 Offset) const;

    void GatherDependencies_Implementation(TArray<UObject*>& Dependencies) override;
    void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
    bool NeedTransform_Implementation() override { return false; }
    bool ShouldSave_Implementation() const override { return !Records.IsEmpty(); }

protected:
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    int32 AddRecord(const FGuid& Id, FName Species, const FString& JobId);
    void BindActor(AActor& Actor, int32 RecordIndex);
    void FinishRecord(int32 RecordIndex, EFactoryLiveMobState State);
    void QueueStingerVisibility(AActor* Actor);

    FDelegateHandle ActorSpawnHandle;
    TSet<TWeakObjectPtr<AActor>> PendingVisibility;
    float DrawDistanceScale = 1.0f;
    float LargeStingerMinimumDistance = 0.0f;

    UFUNCTION()
    void OnCreatureDied(AActor* Actor);

    UFUNCTION()
    void OnMobDestroyed(AActor* Actor);

    UPROPERTY(SaveGame)
    TArray<FFactoryLiveMobRecord> Records;

    struct FCorpse
    {
        TWeakObjectPtr<AActor> Actor;
        int32 Cost;
    };

    TMap<TObjectKey<AActor>, int32> Living;
    TArray<FCorpse> Corpses;
    int32 LivingCost = 0;
    int32 LivingHatchers = 0;
    int32 KilledCount = 0;
    int32 MissingCount = 0;
};

UCLASS()
class URootGameWorld_FactoryLive final : public UGameWorldModule
{
    GENERATED_BODY()

public:
    URootGameWorld_FactoryLive();
};
