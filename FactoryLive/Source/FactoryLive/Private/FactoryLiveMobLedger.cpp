#include "FactoryLiveMobLedger.h"
#include "FactoryLiveBackpack.h"
#include "FactoryLiveVisibility.h"
#include "FactoryLiveFliers.h"
#include "FGSplinePath.h"

#include "Components/MeshComponent.h"
#include "Creature/FGCreature.h"
#include "Creature/Enemy/FGCrabHatcher.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGHealthComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDevice.h"
#include "Subsystem/SubsystemActorManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogFactoryLiveMobs, Log, All);

namespace
{
    const TCHAR* StateName(EFactoryLiveMobState State)
    {
        switch (State)
        {
        case EFactoryLiveMobState::Alive: return TEXT("alive");
        case EFactoryLiveMobState::Killed: return TEXT("killed");
        case EFactoryLiveMobState::Missing: return TEXT("missing");
        }
        return TEXT("unknown");
    }

    bool IsDead(const AActor& Actor)
    {
        if (const AFGCreature* Creature = Cast<AFGCreature>(&Actor))
        {
            const UFGHealthComponent* Health = Creature->GetHealthComponent();
            return Health && Health->IsDead();
        }
        const AFGCrabHatcher* Hatcher = Cast<AFGCrabHatcher>(&Actor);
        return Hatcher && Hatcher->mCurrentHealth <= 0.0f;
    }

    bool IsLargeStinger(const AActor& Actor)
    {
        static const FName AdultClass(TEXT("Char_Stinger_C"));
        static const FName EliteClass(TEXT("Char_EliteStinger_C"));
        const UClass* CreatureClass = AFGCreature::StaticClass();
        if (!Actor.IsA(CreatureClass)) return false;
        for (const UClass* Type = Actor.GetClass(); Type && Type != CreatureClass; Type = Type->GetSuperClass())
        {
            if (Type->GetFName() == AdultClass || Type->GetFName() == EliteClass) return true;
        }
        return false;
    }
}

AActor* FFactoryLiveMobRecord::GetActor() const
{
    if (Hatcher) return Hatcher.Get();
    if (bManagedFlier) return Flier.Get();
    return Actor.Get();
}

URootGameWorld_FactoryLive::URootGameWorld_FactoryLive()
{
    bRootModule = true;
    ModSubsystems.Add(AFactoryLiveMobLedger::StaticClass());
    ModSubsystems.Add(AFactoryLiveBackpack::StaticClass());
}

AFactoryLiveMobLedger::AFactoryLiveMobLedger()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}

AFactoryLiveMobLedger* AFactoryLiveMobLedger::Get(UWorld* World)
{
    auto* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
    return Manager ? Manager->GetSubsystemActor<AFactoryLiveMobLedger>() : nullptr;
}

void AFactoryLiveMobLedger::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    if (World && World->GetNetMode() != NM_DedicatedServer)
    {
        float Scale = FactoryLiveVisibility::DefaultScale;
        float LargeStingerMeters = FactoryLiveVisibility::DefaultStingerMinimumMeters;
        GConfig->GetFloat(TEXT("FactoryLive"), TEXT("CreatureDrawDistanceScale"), Scale, GGameIni);
        GConfig->GetFloat(TEXT("FactoryLive"), TEXT("BigStingerMinDrawDistanceMeters"), LargeStingerMeters, GGameIni);
        DrawDistanceScale = FMath::IsFinite(Scale) ? FMath::Clamp(Scale, 1.0f, 3.0f) : FactoryLiveVisibility::DefaultScale;
        LargeStingerMinimumDistance = (FMath::IsFinite(LargeStingerMeters) ? FMath::Clamp(LargeStingerMeters, 0.0f, 1000.0f)
            : FactoryLiveVisibility::DefaultStingerMinimumMeters) * 100.0f;
        if (DrawDistanceScale > 1.0f)
        {
            ActorSpawnHandle = World->AddOnActorSpawnedHandler(
                FOnActorSpawned::FDelegate::CreateUObject(this, &AFactoryLiveMobLedger::QueueStingerVisibility));
            for (TActorIterator<AFGCreature> It(World); It; ++It) QueueStingerVisibility(*It);
        }
    }

    for (int32 Index = 0; Index < Records.Num(); ++Index)
    {
        FFactoryLiveMobRecord& Record = Records[Index];
        AActor* Actor = Record.GetActor();
        switch (Record.State)
        {
        case EFactoryLiveMobState::Alive:
            if (Record.bManagedFlier)
            {
                PendingFliers.Add(Index);
                LivingCost += GetCreatureCost(Record.Species);
                break;
            }
            if (!IsValid(Actor))
            {
                FinishRecord(Index, EFactoryLiveMobState::Missing);
            }
            else if (IsDead(*Actor))
            {
                FinishRecord(Index, EFactoryLiveMobState::Killed);
            }
            else
            {
                LivingCost += GetCreatureCost(Record.Species);
                BindActor(*Actor, Index);
            }
            break;
        case EFactoryLiveMobState::Killed:
            ++KilledCount;
            break;
        case EFactoryLiveMobState::Missing:
            ++MissingCount;
            break;
        }
    }
}

void AFactoryLiveMobLedger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld(); World && ActorSpawnHandle.IsValid())
        World->RemoveOnActorSpawnedHandler(ActorSpawnHandle);
    PendingVisibility.Reset();
    Super::EndPlay(EndPlayReason);
}

void AFactoryLiveMobLedger::QueueStingerVisibility(AActor* Actor)
{
    if (IsValid(Actor) && IsLargeStinger(*Actor)) PendingVisibility.Add(Actor);
}

void AFactoryLiveMobLedger::ApplyPendingVisibility()
{
    constexpr int32 ActorsPerFrame = 4;
    int32 Checked = 0;
    for (auto It = PendingVisibility.CreateIterator(); It && Checked < ActorsPerFrame; ++It, ++Checked)
    {
        AActor* Actor = It->Get();
        if (Actor && !Actor->HasActorBegunPlay()) continue;
        It.RemoveCurrent();
        if (!IsValid(Actor) || IsDead(*Actor)) continue;

        const float Minimum = IsLargeStinger(*Actor) ? LargeStingerMinimumDistance : 0.0f;
        TInlineComponentArray<UMeshComponent*, 4> Meshes;
        Actor->GetComponents(Meshes);
        for (UMeshComponent* Mesh : Meshes)
        {
            const float Current = Mesh->CachedMaxDrawDistance;
            const float Distance = FactoryLiveVisibility::ExtendedDistance(Current, DrawDistanceScale, Minimum);
            if (Distance <= Current) continue;
            // Use the original effective cap, including any local cull-volume restriction.
            Mesh->bAllowCullDistanceVolume = false;
            Mesh->SetCullDistance(Distance);
            Mesh->SetCachedMaxDrawDistance(Distance);
        }
    }
}

bool AFactoryLiveMobLedger::Track(AFGCreature& Creature, FName Species, const FString& JobId)
{
    if (!IsValid(&Creature) || !Creature.GetHealthComponent() || IsDead(Creature)) return false;

    Creature.SetIsPersistent(true);
    if (!IFGSaveInterface::Execute_ShouldSave(&Creature))
    {
        UE_LOG(LogFactoryLiveMobs, Error, TEXT("%s could not be registered as a saveable creature"), *Species.ToString());
        return false;
    }

    const int32 Index = AddRecord(FGuid::NewGuid(), Species, JobId);
    Records[Index].Actor = &Creature;
    BindActor(Creature, Index);
    return true;
}

bool AFactoryLiveMobLedger::Track(AFGCrabHatcher& Hatcher, FName Species, const FString& JobId)
{
    if (!IsValid(&Hatcher) || IsDead(Hatcher) || !IFGSaveInterface::Execute_ShouldSave(&Hatcher))
    {
        UE_LOG(LogFactoryLiveMobs, Warning, TEXT("Hatcher could not be registered as a living, saveable actor"));
        return false;
    }
    const int32 Index = AddRecord(FGuid::NewGuid(), Species, JobId);
    Records[Index].Hatcher = &Hatcher;
    BindActor(Hatcher, Index);
    return true;
}

void AFactoryLiveMobLedger::TrackFlier(AActor& Actor, AFGSplinePath* Route, const FTransform& Home,
    FName Species, const FString& JobId)
{
    const int32 Index = AddRecord(FGuid::NewGuid(), Species, JobId);
    auto& Record = Records[Index];
    Record.bManagedFlier = true;
    Record.FlightHome = Home;
    Record.FlightPosition = Actor.GetActorLocation();
    Record.Flier = &Actor;
    Record.FlightRoute = Route;
    BindActor(Actor, Index);
}

FName AFactoryLiveMobLedger::NextFlierToRestore() const
{
    return PendingFliers.IsEmpty() ? NAME_None : Records[PendingFliers.Last()].Species;
}

void AFactoryLiveMobLedger::RestoreNextFlier(UClass* Class, AFGCharacterPlayer& Player)
{
    if (PendingFliers.IsEmpty()) return;
    const int32 Index = PendingFliers.Pop(EAllowShrinking::No);
    auto& Record = Records[Index];
    AFGSplinePath* Route = nullptr;
    Record.Flier = FactoryLiveFliers::Spawn(*GetWorld(), Class, Record.FlightHome, Record.FlightPosition, Player, Route);
    if (IsValid(Record.Flier))
    {
        Record.FlightRoute = Route;
        BindActor(*Record.Flier, Index);
    }
    else
    {
        LivingCost -= GetCreatureCost(Record.Species);
        FinishRecord(Index, EFactoryLiveMobState::Missing);
    }
}

int32 AFactoryLiveMobLedger::AddRecord(const FGuid& Id, FName Species, const FString& JobId)
{
    const int32 Index = Records.AddDefaulted();
    FFactoryLiveMobRecord& Record = Records[Index];
    Record.Id = Id;
    Record.JobId = JobId;
    Record.Species = Species;
    LivingCost += GetCreatureCost(Species);
    UE_LOG(LogFactoryLiveMobs, Display, TEXT("Spawned %s %s (job %s)"),
        *Species.ToString(), *Record.Id.ToString(), *JobId);
    return Index;
}

void AFactoryLiveMobLedger::BindActor(AActor& Actor, int32 RecordIndex)
{
    Living.Add(TObjectKey<AActor>(&Actor), RecordIndex);
    if (DrawDistanceScale > 1.0f) PendingVisibility.Add(&Actor);
    if (Records[RecordIndex].Hatcher) ++LivingHatchers;
    if (AFGCreature* Creature = Cast<AFGCreature>(&Actor))
    {
        if (UFGHealthComponent* Health = Creature->GetHealthComponent())
        {
            Health->DeathDelegate.AddUniqueDynamic(this, &AFactoryLiveMobLedger::OnCreatureDied);
        }
    }
    Actor.OnDestroyed.AddUniqueDynamic(this, &AFactoryLiveMobLedger::OnMobDestroyed);
}

void AFactoryLiveMobLedger::FinishRecord(int32 RecordIndex, EFactoryLiveMobState State)
{
    FFactoryLiveMobRecord& Record = Records[RecordIndex];
    if (Record.State != EFactoryLiveMobState::Alive) return;

    AActor* Actor = Record.GetActor();
    const int32 Cost = GetCreatureCost(Record.Species);
    const bool bWasBound = Actor && Living.Remove(TObjectKey<AActor>(Actor)) > 0;
    if (bWasBound)
    {
        LivingCost -= Cost;
        if (Record.Hatcher) --LivingHatchers;
    }
    Record.Actor = nullptr;
    Record.Hatcher = nullptr;
    Record.Flier = nullptr;
    if (IsValid(Record.FlightRoute)) Record.FlightRoute->Destroy();
    Record.FlightRoute = nullptr;
    Record.State = State;
    if (State == EFactoryLiveMobState::Killed)
    {
        ++KilledCount;
        if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Corpses.Add({Actor, Cost});
    }
    else
    {
        ++MissingCount;
    }
    UE_LOG(LogFactoryLiveMobs, Display, TEXT("Mob %s %s %s (job %s)"),
        *Record.Id.ToString(), *Record.Species.ToString(), StateName(State), *Record.JobId);
}

void AFactoryLiveMobLedger::OnCreatureDied(AActor* Actor)
{
    const int32* Index = Living.Find(TObjectKey<AActor>(Actor));
    if (Index) FinishRecord(*Index, EFactoryLiveMobState::Killed);
}

void AFactoryLiveMobLedger::OnMobDestroyed(AActor* Actor)
{
    if (!GetWorld() || GetWorld()->bIsTearingDown) return;
    const int32* Index = Living.Find(TObjectKey<AActor>(Actor));
    if (Index)
        FinishRecord(*Index, IsDead(*Actor) ? EFactoryLiveMobState::Killed : EFactoryLiveMobState::Missing);
}

int32 AFactoryLiveMobLedger::GetCreatureCost(FName Species)
{
    static const TMap<FName, int32> Costs = {
        {FName(TEXT("alpha-hog")), 3}, {FName(TEXT("jetpack-hog")), 3},
        {FName(TEXT("cliff-hog")), 4}, {FName(TEXT("nuclear-hog")), 6},
        {FName(TEXT("stinger")), 2}, {FName(TEXT("big-stinger")), 4}, {FName(TEXT("elite-stinger")), 5},
        {FName(TEXT("spitter")), 2}, {FName(TEXT("spitter-red")), 2},
        {FName(TEXT("spitter-desert")), 2}, {FName(TEXT("spitter-aquatic")), 2},
        {FName(TEXT("alpha-spitter")), 4}, {FName(TEXT("alpha-spitter-red")), 4},
        {FName(TEXT("alpha-spitter-desert")), 4}, {FName(TEXT("alpha-spitter-aquatic")), 4},
        {FName(TEXT("crab-hatcher")), 12}, {FName(TEXT("elite-hatcher")), 12},
        {FName(TEXT("bean")), 6}, {FName(TEXT("manta")), 24}
    };
    const int32* Cost = Costs.Find(Species);
    return Cost ? *Cost : 1;
}

int32 AFactoryLiveMobLedger::GetOccupiedCost()
{
    Corpses.RemoveAllSwap([](const FCorpse& Corpse) { return !Corpse.Actor.IsValid(); }, EAllowShrinking::No);
    int32 Cost = LivingCost;
    for (const FCorpse& Corpse : Corpses) Cost += Corpse.Cost;
    return Cost;
}

void AFactoryLiveMobLedger::GatherDependencies_Implementation(TArray<UObject*>& Dependencies)
{
    for (const auto& Entry : Living)
    {
        AActor* Actor = Records[Entry.Value].GetActor();
        if (IsValid(Actor) && !Records[Entry.Value].bManagedFlier && IFGSaveInterface::Execute_ShouldSave(Actor)) Dependencies.Add(Actor);
    }
}

void AFactoryLiveMobLedger::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
    TArray<int32, TInlineAllocator<24>> DeadRecords;
    for (const auto& Entry : Living)
    {
        const AActor* Actor = Records[Entry.Value].GetActor();
        if (IsValid(Actor) && Records[Entry.Value].bManagedFlier)
            Records[Entry.Value].FlightPosition = Actor->GetActorLocation();
        if (IsValid(Actor) && IsDead(*Actor)) DeadRecords.Add(Entry.Value);
    }
    for (const int32 Index : DeadRecords) FinishRecord(Index, EFactoryLiveMobState::Killed);
}

void AFactoryLiveMobLedger::PrintHistory(FOutputDevice& Output, int32 Offset) const
{
    Output.Logf(TEXT("Factory Live mobs: %d spawned, %d alive, %d killed, %d missing"),
        GetSpawnedCount(), GetAliveCount(), GetKilledCount(), GetMissingCount());
    for (int32 Index = Records.Num() - 1 - Offset, Shown = 0; Index >= 0 && Shown < 25; --Index, ++Shown)
    {
        const FFactoryLiveMobRecord& Record = Records[Index];
        Output.Logf(TEXT("%s  %s  %s  job %s"),
            *Record.Id.ToString(), *Record.Species.ToString(), StateName(Record.State), *Record.JobId);
    }
}
