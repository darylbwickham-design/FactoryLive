#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Containers/UnrealString.h"
#include "Creature/FGCreature.h"
#include "Creature/Enemy/FGCrabHatcher.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Equipment/FGAmmoTypeProjectile.h"
#include "Equipment/FGStickyExplosive.h"
#include "FGInventoryComponentEquipment.h"
#include "Creature/Enemy/FGFlyingBabyCrab.h"
#include "FGManta.h"
#include "FGItemPickup_Spawnable.h"
#include "FGPlayerController.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "FactoryLiveBackpack.h"
#include "FactoryLiveBudget.h"
#include "FactoryLiveFliers.h"
#include "FactoryLiveMobLedger.h"
#include "FactoryLiveFireworks.h"
#include "FactoryLiveNobeliskImpact.h"
#include "FGCharacterPlayer.h"
#include "FGCreatureSubsystem.h"
#include "FGProjectileMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogFactoryLive, Log, All);

namespace
{
    constexpr int32 MaxJobs = 4;
    constexpr double NobeliskLaunchInterval = 0.22;
    constexpr double NobeliskFlightTimeout = 5.0;
    constexpr int32 MaxNukesPerWave = 5;
    constexpr double NukeWaveLaunchInterval = 4.0;
    constexpr double CreatureCapacityRecheck = 0.15;
    constexpr double BackpackCommandTimeout = 2.0;
    constexpr double TurnAroundInterval = 20.0;
    constexpr double EquipmentDropInterval = 45.0;
    constexpr double JobTimeout = 900.0;
    constexpr double BridgeTimeout = 3.0;
    constexpr double CreatureHeightAbovePlayer = 50.0;
    constexpr double MaxCreatureLandingDrop = 300.0;
    constexpr double CreatureGroundProbeUp = 80.0;
    constexpr int32 MaxPlacementFailures = 32;
    constexpr int32 HatcherCapacity = 600;

    enum class ECreatureKind : uint8 { Hostile, Wildlife, Hatcher, FlyingCrab, Manta };
    struct FCreatureType
    {
        const TCHAR* Name;
        const TCHAR* ClassPath;
        ECreatureKind Kind;
    };
    constexpr FCreatureType CreatureTypes[] = {
        { TEXT("hog"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Hog/Char_Hog.Char_Hog_C"), ECreatureKind::Hostile },
        { TEXT("alpha-hog"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Hog/AlphaHog/Char_AlphaHog.Char_AlphaHog_C"), ECreatureKind::Hostile },
        { TEXT("spitter"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterForest/Char_SpitterForestSmall.Char_SpitterForestSmall_C"), ECreatureKind::Hostile },
        { TEXT("alpha-spitter"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterForest/Char_SpitterForestAlpha.Char_SpitterForestAlpha_C"), ECreatureKind::Hostile },
        { TEXT("jetpack-hog"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Hog/Johnny/Char_Johnny.Char_Johnny_C"), ECreatureKind::Hostile },
        { TEXT("stinger"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Stinger/SmallStinger/Char_Stinger_Child.Char_Stinger_Child_C"), ECreatureKind::Hostile },
        { TEXT("elite-stinger"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Stinger/BigStinger/Char_EliteStinger.Char_EliteStinger_C"), ECreatureKind::Hostile },
        { TEXT("cliff-hog"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Hog/CliffHog/Char_CliffHog.Char_CliffHog_C"), ECreatureKind::Hostile },
        { TEXT("nuclear-hog"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Hog/NuclearHog/Char_NuclearHog.Char_NuclearHog_C"), ECreatureKind::Hostile },
        { TEXT("big-stinger"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Stinger/Char_Stinger.Char_Stinger_C"), ECreatureKind::Hostile },
        { TEXT("spitter-red"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterForestRed/Char_SpitterForestRedSmall.Char_SpitterForestRedSmall_C"), ECreatureKind::Hostile },
        { TEXT("spitter-desert"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterDesert/Char_SpitterDesertSmall.Char_SpitterDesertSmall_C"), ECreatureKind::Hostile },
        { TEXT("spitter-aquatic"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterAquatic/Char_SpitterAquaticSmall.Char_SpitterAquaticSmall_C"), ECreatureKind::Hostile },
        { TEXT("alpha-spitter-red"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterForestRed/Char_SpitterForestRedAlpha.Char_SpitterForestRedAlpha_C"), ECreatureKind::Hostile },
        { TEXT("alpha-spitter-desert"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterDesert/Char_SpitterDesertAlpha.Char_SpitterDesertAlpha_C"), ECreatureKind::Hostile },
        { TEXT("alpha-spitter-aquatic"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Spitter/SpitterAquatic/Char_SpitterAquaticAlpha.Char_SpitterAquaticAlpha_C"), ECreatureKind::Hostile },
        { TEXT("crab-hatcher"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/CrabHatcher/Char_CrabHatcher.Char_CrabHatcher_C"), ECreatureKind::Hatcher },
        { TEXT("elite-hatcher"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/CrabHatcher/Char_BigCrabHatcher.Char_BigCrabHatcher_C"), ECreatureKind::Hatcher },
        { TEXT("flying-crab"), TEXT("/Game/FactoryGame/Character/Creature/Enemy/Crab/BabyCrab/BP_FlyingBabyCrab.BP_FlyingBabyCrab_C"), ECreatureKind::FlyingCrab },
        { TEXT("lizard-doggo"), TEXT("/Game/FactoryGame/Character/Creature/Wildlife/SpaceRabbit/Char_SpaceRabbit.Char_SpaceRabbit_C"), ECreatureKind::Wildlife },
        { TEXT("bean"), TEXT("/Game/FactoryGame/Character/Creature/Wildlife/SpaceGiraffe/Char_SpaceGiraffe.Char_SpaceGiraffe_C"), ECreatureKind::Wildlife },
        { TEXT("flightless-bird"), TEXT("/Game/FactoryGame/Character/Creature/Wildlife/NonFlyingBird/Char_NonFlyingBird.Char_NonFlyingBird_C"), ECreatureKind::Wildlife },
        { TEXT("beetle"), TEXT("/Game/FactoryGame/Character/Creature/Wildlife/Beetle/Char_Beetle.Char_Beetle_C"), ECreatureKind::Wildlife },
        { TEXT("manta"), TEXT("/Game/FactoryGame/Character/Creature/Wildlife/Manta/BP_Manta.BP_Manta_C"), ECreatureKind::Manta }
    };
    static int32 FindCreatureIndex(const FString& Creature)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(CreatureTypes); ++Index)
        {
            if (Creature == CreatureTypes[Index].Name) return Index;
        }
        return INDEX_NONE;
    }
    struct FNobeliskType
    {
        const TCHAR* Name;
        const TCHAR* AmmoPath;
        double Recovery;
        double RepeatInterval;
        int32 MaxPerJob;
        bool bExclusive;
    };

    const FNobeliskType NobeliskTypes[] = {
        {TEXT("standard"), TEXT("/Game/FactoryGame/Resource/Parts/NobeliskExplosive/Desc_NobeliskExplosive.Desc_NobeliskExplosive_C"),
            0.12, 0.0, FactoryLiveBudget::MaxPendingNobelisks, false},
        {TEXT("gas"), TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskGas.Desc_NobeliskGas_C"),
            0.85, 0.0, FactoryLiveBudget::MaxPendingNobelisks, true},
        {TEXT("pulse"), TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskShockwave.Desc_NobeliskShockwave_C"),
            0.30, 0.0, FactoryLiveBudget::MaxPendingNobelisks, true},
        {TEXT("cluster"), TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskCluster.Desc_NobeliskCluster_C"),
            0.45, 0.0, FactoryLiveBudget::MaxPendingNobelisks, true},
        {TEXT("nuke"), TEXT("/Game/FactoryGame/Equipment/NobeliskDetonator/Ammo/Desc_NobeliskNuke.Desc_NobeliskNuke_C"),
            1.50, 15.0, MaxNukesPerWave, true}
    };
    constexpr int32 NukeTypeIndex = UE_ARRAY_COUNT(NobeliskTypes) - 1;

    struct FFallingNobelisk
    {
        TWeakObjectPtr<AFGStickyExplosive> Actor;
        TStrongObjectPtr<UFactoryLiveNobeliskImpact> Impact;
        int32 TypeIndex;
        double Expires;
        bool bMoreNukesInWave = false;
        bool bFireworks = false;
    };

    struct FJob
    {
        FString Id;
        int32 CreatureIndex = INDEX_NONE;
        int32 CreatureCost = 0;
        TArray<int32, TInlineAllocator<8>> CreatureVariants;
        int32 NobeliskIndex = INDEX_NONE;
        int32 Requested = 0;
        int32 Remaining = 0;
        int32 ConsecutiveFailures = 0;
        int32 PlacementIndex = 0;
        float PlacementAngle = 0.0f;
        FVector FireworkOrigin = FVector::ZeroVector;
        double Expires = 0;
        double RetryAt = 0;
        bool bPreferGroundPlacement = true;
        bool bFireworks = false;
        bool IsNobelisk() const { return bFireworks || NobeliskIndex != INDEX_NONE; }
        bool IsNuke() const { return NobeliskIndex == NukeTypeIndex; }
        bool IsHatcher() const { return CreatureIndex != INDEX_NONE && CreatureTypes[CreatureIndex].Kind == ECreatureKind::Hatcher; }
        int32 CurrentCreatureIndex() const
        {
            return CreatureVariants.IsEmpty() ? CreatureIndex : CreatureVariants[(Requested - Remaining) % CreatureVariants.Num()];
        }
    };

    struct FPendingWork
    {
        int32 Creatures = 0;
        int32 Nobelisks = 0;
        int32 Nukes = 0;
        int32 Hatchers = 0;
        bool bFireworks = false;
    };

    struct FBackpackCommand
    {
        FString Id;
        FString Action;
        double Expires = 0.0;
    };

    bool ReadJson(const FString& Text, TSharedPtr<FJsonObject>& Result)
    {
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result) && Result.IsValid();
    }
}

class FFactoryLiveModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        MobCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("FactoryLive.Mobs"),
            TEXT("Show the save's creature history. Optional argument: number of recent entries to skip."),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryLiveModule::ShowMobs),
            ECVF_Default);
        FString Directory = FPlatformMisc::GetEnvironmentVariable(TEXT("FACTORYLIVE_CHAOS_HOME"));
        if (Directory.IsEmpty())
        {
            const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
            if (LocalAppData.IsEmpty()) return;
            Directory = FPaths::Combine(LocalAppData, TEXT("FactoryLiveChaos"));
        }
        LinkPath = FPaths::Combine(Directory, TEXT("link.json"));
#if PLATFORM_WINDOWS
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FactoryLive")))
        {
            BundledBridgeDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Bridge")));
        }
#endif
        TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FFactoryLiveModule::Tick);
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FFactoryLiveModule::OnCleanup);
    }

    void ShutdownModule() override
    {
        if (MobCommand) IConsoleManager::Get().UnregisterConsoleObject(MobCommand);
        FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
        FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        ResetSession();
        AssetLoad.Reset();
    }

private:
    static void ShowMobs(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
    {
        const AFactoryLiveMobLedger* Ledger = AFactoryLiveMobLedger::Get(World);
        if (!Ledger)
        {
            Output.Log(TEXT("Factory Live mob history is available in the host's loaded save."));
            return;
        }
        int32 Offset = 0;
        if (Args.Num() > 1 || (Args.Num() == 1 && (!LexTryParseString(Offset, *Args[0]) || Offset < 0)))
        {
            Output.Log(TEXT("Usage: FactoryLive.Mobs [nonnegative offset]"));
            return;
        }
        Ledger->PrintHistory(Output, Offset);
    }

    void OnCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
    {
        if (World == ActiveWorld.Get()) ResetSession();
    }

    void ResetSession()
    {
        if (Request)
        {
            Request->OnProcessRequestComplete().Unbind();
            Request->CancelRequest();
            Request.Reset();
        }
        Disarm();
        StopBundledBridge();
        while (!FallingNobelisks.IsEmpty()) CancelLastDrop();
        bCancelFallingDrops = false;
        ActiveWorld.Reset();
        Session.Reset();
        GameToken.Reset();
        Endpoint.Reset();
        Epoch.Reset();
        LastAccepted.Reset();
        LastBackpackId.Reset();
        LastBackpackAction.Reset();
        LastBackpackCode.Reset();
        LastBackpackStacks = 0;
        LastCreatureJob = {};
        LastCreatureJobState = nullptr;
        LaunchedNobelisks = 0;
        ImpactedNobelisks = 0;
        ExpiredNobelisks = 0;
        CancelledNobelisks = 0;
        FrameBudget = {};
        RespawnGuard = {};
        LastSync = 0;
        NextSync = 0;
        NextCreature = 0;
        NextNobelisk = 0;
        NextFireworkImpact = 0;
        NextEquipmentDrop = 0;
        NextTurnAround = 0;
        for (double& Next : NextNobeliskType) Next = 0.0;
    }

    void Disarm()
    {
        if (bArmed) UE_LOG(LogFactoryLive, Display, TEXT("Stream effects disarmed"));
        bArmed = false;
        ClearQueuedEffects();
    }

    void ClearQueuedEffects()
    {
        PendingBackpack.Reset();
        Jobs.Reset();
        bWorldCreatureCapacityBlocked = false;
        bCancelFallingDrops = !FallingNobelisks.IsEmpty();
    }

    void CancelLastDrop()
    {
        const FFallingNobelisk& Drop = FallingNobelisks.Last();
        if (AFGStickyExplosive* Actor = Drop.Actor.Get(); Actor && Actor->GetWorld() && !Actor->GetWorld()->bIsTearingDown)
        {
            Actor->Destroy();
        }
        FallingNobelisks.RemoveAt(FallingNobelisks.Num() - 1, EAllowShrinking::No);
        ++CancelledNobelisks;
    }

    void PrepareAssets()
    {
        if (AssetLoad) return;
        TArray<FSoftObjectPath> Paths;
        for (const FCreatureType& Creature : CreatureTypes) Paths.Emplace(Creature.ClassPath);
        for (const FNobeliskType& Type : NobeliskTypes) Paths.Emplace(Type.AmmoPath);
        AssetLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
    }

    bool AssetsReady()
    {
        if (!AssetLoad || !AssetLoad->HasLoadCompleted()) return false;
        if (!bCheckedAssets)
        {
            bCheckedAssets = true;
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(CreatureTypes); ++Index)
            {
                UClass* Type = Cast<UClass>(FSoftObjectPath(CreatureTypes[Index].ClassPath).ResolveObject());
                const ECreatureKind Kind = CreatureTypes[Index].Kind;
                const UClass* BaseClass = AFGCreature::StaticClass();
                if (Kind == ECreatureKind::Hatcher) BaseClass = AFGCrabHatcher::StaticClass();
                if (Kind == ECreatureKind::FlyingCrab) BaseClass = AFGFlyingBabyCrab::StaticClass();
                if (Kind == ECreatureKind::Manta) BaseClass = AFGManta::StaticClass();
                const bool bValid = Type && Type->IsChildOf(BaseClass) &&
                    !Type->HasAnyClassFlags(CLASS_Abstract);
                if (!bValid)
                {
                    UE_LOG(LogFactoryLive, Error, TEXT("Creature class unavailable: %s"), CreatureTypes[Index].ClassPath);
                    continue;
                }
                CreatureClasses[Index] = Type;
                bAssetsReady = true;
            }
            ExplodedFlag = FindFProperty<FBoolProperty>(AFGProjectile::StaticClass(), TEXT("mHasExploded"));
            if (!ExplodedFlag)
            {
                UE_LOG(LogFactoryLive, Error, TEXT("Nobelisk explosion state unavailable"));
                return bAssetsReady;
            }
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(NobeliskTypes); ++Index)
            {
                const FNobeliskType& Type = NobeliskTypes[Index];
                const UClass* AmmoClass = Cast<UClass>(FSoftObjectPath(Type.AmmoPath).ResolveObject());
                UFGAmmoTypeProjectile* Descriptor = AmmoClass ? AmmoClass->GetDefaultObject<UFGAmmoTypeProjectile>() : nullptr;
                const UClass* Projectile = Descriptor ? Descriptor->GetProjectileClass().Get() : nullptr;
                if (!Projectile || !Projectile->IsChildOf(AFGStickyExplosive::StaticClass()) ||
                    Projectile->HasAnyClassFlags(CLASS_Abstract))
                {
                    UE_LOG(LogFactoryLive, Error, TEXT("Nobelisk projectile class unavailable: %s"), Type.Name);
                    continue;
                }
                NobeliskAmmo[Index] = Descriptor;
                bAssetsReady = true;
            }
        }
        return bAssetsReady;
    }

    void Tick(UWorld* World, ELevelTick TickType, float DeltaTime)
    {
        if (!World || World->WorldType != EWorldType::Game || World->bIsTearingDown ||
            World->GetNetMode() == NM_Client || World->GetNetMode() == NM_DedicatedServer) return;
        AFactoryLiveMobLedger* Ledger = AFactoryLiveMobLedger::Get(World);
        if (!Ledger || !Ledger->HasActorBegunPlay()) return;
        APlayerController* Controller = World->GetFirstPlayerController();
        if (!Controller || !Controller->IsLocalController()) return;
        AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(Controller->GetPawn());
        if (Session.IsEmpty() && !Player) return;

        const double Now = FPlatformTime::Seconds();
        if (World != ActiveWorld.Get())
        {
            ResetSession();
            ActiveWorld = World;
            Session = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        }
        FrameBudget.Observe(Now);
        if (bArmed && Now - LastSync > BridgeTimeout) Disarm();

        const AFGPlayerController* FactoryController = Cast<AFGPlayerController>(Controller);
        const bool bPioneerAlive = Player && Player->IsAliveAndWell();
        const bool bRespawnTransition = (Player && !bPioneerAlive) ||
            (FactoryController && (FactoryController->IsRespawning() || FactoryController->IsClientRespawnGracePeriodActive()));
        if (RespawnGuard.Observe(Now, bRespawnTransition))
        {
            ClearQueuedEffects();
            UE_LOG(LogFactoryLive, Display, TEXT("Cleared stream effects for pioneer respawn"));
        }
        const bool bRespawnHold = RespawnGuard.IsHolding(Now);
        const bool bPlaying = bPioneerAlive && !bRespawnHold && !UGameplayStatics::IsGamePaused(World);
        if (bPlaying && FrameBudget.CanSpawn(Now)) Ledger->ApplyPendingVisibility();
        if (bPlaying) PrepareAssets();
        const bool bReady = bPlaying && AssetsReady();
        if (bReady) StartBundledBridge();
        const bool bSlow = FrameBudget.IsPaused(Now);
        AFactoryLiveBackpack* Backpack = AFactoryLiveBackpack::Get(World);
        const UFGInventoryComponent* Inventory = Player ? Player->GetInventory() : nullptr;
        const bool bBackpackReady = bPlaying && Backpack && Backpack->HasActorBegunPlay() &&
            Inventory && Inventory->HasAuthority() && !Inventory->IsLocked();
        const bool bTurnReady = bPlaying && !Player->GetDrivenVehicle() && !Controller->IsLookInputIgnored();
        const bool bEquipmentDropReady = bPlaying && !Player->GetDrivenVehicle() && Player->GetCharacterMovement()->IsMovingOnGround();
        if (!Request && Now >= NextSync)
        {
            Poll(Now, bReady, bSlow, *Ledger, Backpack, Player, bBackpackReady, bTurnReady, bEquipmentDropReady);
        }
        if (bCancelFallingDrops && !FallingNobelisks.IsEmpty())
        {
            CancelLastDrop();
            return;
        }
        bCancelFallingDrops = false;
        if (bReady && bArmed && ProcessNobeliskImpact(Now, bSlow)) return;
        if (ProcessPlayerAction(Now, bSlow, bBackpackReady, bTurnReady, bEquipmentDropReady, Backpack, Player, *Controller)) return;
        if (bReady && Now >= NextCreature && FrameBudget.CanSpawn(Now))
        {
            const FName Pending = Ledger->NextFlierToRestore();
            if (!Pending.IsNone())
            {
                const int32 Index = FindCreatureIndex(Pending.ToString());
                Ledger->RestoreNextFlier(Index == INDEX_NONE ? nullptr : CreatureClasses[Index].Get(), *Player);
                const double Finished = FPlatformTime::Seconds();
                FrameBudget.ReportWork(Finished, Finished - Now);
                NextCreature = Finished + CreatureCapacityRecheck;
                return;
            }
        }
        if (!bReady || !bArmed || !FrameBudget.CanSpawn(Now)) return;
        ProcessJobs(*World, *Player, *Ledger, Now, bSlow);
    }

    static FFactoryLiveBackpackResult DropRandomEquipment(AFGCharacterPlayer& Player)
    {
        struct FEquipmentItem
        {
            UFGInventoryComponentEquipment* Inventory;
            int32 Index;
            FInventoryStack Stack;
        };
        TArray<FEquipmentItem, TInlineAllocator<8>> Items;
        for (EEquipmentSlot Slot : {EEquipmentSlot::ES_ARMS, EEquipmentSlot::ES_BACK, EEquipmentSlot::ES_LEGS,
                EEquipmentSlot::ES_HEAD, EEquipmentSlot::ES_BODY})
        {
            UFGInventoryComponentEquipment* Inventory = Player.GetEquipmentSlot(Slot);
            if (!Inventory || !Inventory->HasAuthority() || Inventory->IsLocked()) continue;
            for (int32 Index = 0; Index < Inventory->GetSizeLinear(); ++Index)
            {
                FInventoryStack Stack;
                if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems() || Stack.Item.LegacyItemStateActor) continue;
                const UClass* ItemClass = Stack.Item.GetItemClass();
                if (ItemClass && ItemClass->IsChildOf(UFGEquipmentDescriptor::StaticClass()))
                    Items.Add({Inventory, Index, MoveTemp(Stack)});
            }
        }
        if (Items.IsEmpty()) return {TEXT("no_equipment")};
        const FVector Location = Player.GetInventoryDropLocation();
        if (Location.ContainsNaN() || FVector::DistSquared(Location, Player.GetActorLocation()) > FMath::Square(600.0))
            return {TEXT("drop_location_unavailable")};

        const FEquipmentItem& Choice = Items[FMath::RandHelper(Items.Num())];
        FInventoryStack Drop = Choice.Stack;
        Drop.NumItems = 1;
        UWorld* World = Player.GetWorld();
        AFGItemPickup_Spawnable* Pickup = AFGItemPickup_Spawnable::CreateItemDrop(
            World, Drop, Location, FRotator::ZeroRotator, nullptr, World->PersistentLevel);
        if (!IsValid(Pickup)) return {TEXT("drop_failed")};

        Choice.Inventory->RemoveFromIndex(Choice.Index, 1);
        FInventoryStack Remaining;
        Choice.Inventory->GetStackFromIndex(Choice.Index, Remaining);
        if (Remaining.NumItems != Choice.Stack.NumItems - 1)
        {
            Pickup->Destroy();
            return {TEXT("drop_failed")};
        }
        return {TEXT("dropped"), 1};
    }

    bool ProcessPlayerAction(double Now, bool bSlow, bool bBackpackReady, bool bTurnReady, bool bEquipmentDropReady, AFactoryLiveBackpack* Backpack,
        AFGCharacterPlayer* Player, APlayerController& Controller)
    {
        if (!PendingBackpack) return false;
        const bool bTurn = PendingBackpack->Action == TEXT("turn-around");
        const bool bDrop = PendingBackpack->Action == TEXT("equipment-drop");
        const bool bReady = bTurn ? bArmed && bTurnReady : bDrop ? bArmed && bEquipmentDropReady : bBackpackReady;
        const bool bExpired = Now >= PendingBackpack->Expires;
        if (!bExpired && (!bReady || bSlow)) return false;
        FBackpackCommand Command = MoveTemp(*PendingBackpack);
        PendingBackpack.Reset();
        FFactoryLiveBackpackResult Result{bTurn ? TEXT("turn_unavailable") : bDrop ? TEXT("drop_unavailable") : TEXT("inventory_unavailable")};
        if (!bExpired)
        {
            if (bTurn)
            {
                if (Now >= NextTurnAround)
                {
                    FRotator View = Controller.GetControlRotation();
                    View.Yaw = FRotator::NormalizeAxis(View.Yaw + 180.0);
                    Controller.SetControlRotation(View);
                    NextTurnAround = Now + TurnAroundInterval;
                    Result = {TEXT("turned")};
                }
                else Result = {TEXT("viewer_cooldown")};
            }
            else if (bDrop)
            {
                if (Now >= NextEquipmentDrop)
                {
                    NextEquipmentDrop = Now + EquipmentDropInterval;
                    Result = DropRandomEquipment(*Player);
                }
                else Result = {TEXT("viewer_cooldown")};
            }
            else
            {
                Result = Backpack->Execute(*Player, Command.Action);
            }
        }
        LastBackpackId = MoveTemp(Command.Id);
        LastBackpackAction = MoveTemp(Command.Action);
        LastBackpackCode = Result.Code;
        LastBackpackStacks = Result.Stacks;
        const double Finished = FPlatformTime::Seconds();
        FrameBudget.ReportWork(Finished, Finished - Now);
        UE_LOG(LogFactoryLive, Display, TEXT("Player action %s: %s (%d stacks)"),
            *LastBackpackAction, *LastBackpackCode, LastBackpackStacks);
        return true;
    }

    bool ProcessNobeliskImpact(double Now, bool bSlow)
    {
        for (int32 Index = 0; Index < FallingNobelisks.Num(); ++Index)
        {
            const FFallingNobelisk& Drop = FallingNobelisks[Index];
            AFGStickyExplosive* Actor = Drop.Actor.Get();
            const bool bExploded = Actor && ExplodedFlag->GetPropertyValue_InContainer(Actor);
            if (!Actor || bExploded || Now >= Drop.Expires)
            {
                if (Actor && !bExploded) Actor->Destroy();
                ++ExpiredNobelisks;
                FallingNobelisks.RemoveAt(Index);
                return true;
            }
            if ((Drop.bFireworks ? Now < NextFireworkImpact : bSlow) || !Drop.Impact->HasImpact()) continue;

            const int32 TypeIndex = Drop.TypeIndex;
            const FNobeliskType& Type = NobeliskTypes[TypeIndex];
            const bool bMoreNukesInWave = Drop.bMoreNukesInWave;
            const bool bFireworks = Drop.bFireworks;
            const bool bUseRegularRecovery = !bFireworks || TypeIndex == NukeTypeIndex;
            Actor->TriggerSecondary();
            ++ImpactedNobelisks;
            FallingNobelisks.RemoveAt(Index);
            const double Finished = FPlatformTime::Seconds();
            FrameBudget.ReportWork(Finished, Finished - Now);
            if (bFireworks) NextFireworkImpact = Finished + FrameBudget.SpawnDelay(FactoryLiveFireworks::ImpactInterval);
            if (bUseRegularRecovery)
            {
                NextNobelisk = FMath::Max(NextNobelisk, Finished + Type.Recovery);
                if (!bMoreNukesInWave) NextNobeliskType[TypeIndex] = Finished + Type.RepeatInterval;
            }
            return true;
        }
        return false;
    }

    void StartBundledBridge()
    {
        if (bBundledBridgeAttempted || BundledBridgeDirectory.IsEmpty()) return;
        bBundledBridgeAttempted = true;
        const FString Runtime = FPaths::Combine(BundledBridgeDirectory, TEXT("node.exe"));
        const FString Script = FPaths::Combine(BundledBridgeDirectory, TEXT("bridge.mjs"));
        if (!IFileManager::Get().FileExists(*Runtime) || !IFileManager::Get().FileExists(*Script)) return;
        const FString Arguments = FString::Printf(TEXT("\"%s\" serve-host %u"), *Script, FPlatformProcess::GetCurrentProcessId());
        BundledBridge = FPlatformProcess::CreateProc(*Runtime, *Arguments, false, true, true, nullptr, 0, *BundledBridgeDirectory, nullptr);
        if (!BundledBridge.IsValid()) UE_LOG(LogFactoryLive, Warning, TEXT("Could not start the bundled Factory Live bridge"));
    }

    void StopBundledBridge()
    {
        if (BundledBridge.IsValid())
        {
            if (FPlatformProcess::IsProcRunning(BundledBridge)) FPlatformProcess::TerminateProc(BundledBridge);
            FPlatformProcess::CloseProc(BundledBridge);
            BundledBridge.Reset();
        }
        bBundledBridgeAttempted = false;
    }

    bool LoadLink()
    {
        const int64 Size = IFileManager::Get().FileSize(*LinkPath);
        FString Text;
        TSharedPtr<FJsonObject> Json;
        int32 Port = 0;
        FString Token;
        if (Size <= 0 || Size > 4096 || !FFileHelper::LoadFileToString(Text, *LinkPath) || !ReadJson(Text, Json) ||
            !Json->TryGetNumberField(TEXT("port"), Port) || Port < 1024 || Port > 65535 ||
            !Json->TryGetStringField(TEXT("gameToken"), Token) || Token.Len() != 64)
        {
            return false;
        }
        for (const TCHAR Char : Token)
        {
            if (!FChar::IsHexDigit(Char)) return false;
        }
        Endpoint = FString::Printf(TEXT("http://127.0.0.1:%d/v1/game"), Port);
        GameToken = MoveTemp(Token);
        return true;
    }

    void Poll(double Now, bool bReady, bool bSlow, AFactoryLiveMobLedger& Ledger,
        const AFactoryLiveBackpack* Backpack, const AFGCharacterPlayer* Player, bool bBackpackReady, bool bTurnReady, bool bEquipmentDropReady)
    {
        NextSync = Now + 0.4;
        if (GameToken.IsEmpty() && !LoadLink())
        {
            NextSync = Now + 2.0;
            return;
        }
        const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
        Body->SetStringField(TEXT("session"), Session);
        Body->SetStringField(TEXT("ack"), LastAccepted);
        Body->SetBoolField(TEXT("ready"), bReady);
        if (RespawnGuard.IsHolding(Now)) Body->SetBoolField(TEXT("respawning"), true);
        Body->SetBoolField(TEXT("slow"), bSlow);
        Body->SetNumberField(TEXT("free"), MaxJobs - Jobs.Num());
        Body->SetNumberField(TEXT("active"), Ledger.GetAliveCount());
        const FPendingWork Work = GetPendingWork();
        const TSharedRef<FJsonObject> Pending = MakeShared<FJsonObject>();
        Pending->SetNumberField(TEXT("creatures"), Work.Creatures);
        Pending->SetNumberField(TEXT("cluster"), Work.Nobelisks);
        Pending->SetNumberField(TEXT("nuke"), Work.Nukes);
        if (Work.bFireworks) Pending->SetBoolField(TEXT("fireworks"), true);
        Body->SetObjectField(TEXT("pending"), Pending);
        TArray<TSharedPtr<FJsonValue>> CreatureJobs;
        if (Work.Creatures > 0)
        {
            const int32 LivingCost = Ledger.GetLivingCost();
            const int32 OccupiedCost = Ledger.GetOccupiedCost();
            for (const FJob& Job : Jobs)
            {
                if (Job.IsNobelisk()) continue;
                const TCHAR* State = TEXT("running");
                if (!bReady) State = TEXT("waiting_for_game");
                else if (bWorldCreatureCapacityBlocked ||
                    (Job.IsHatcher() && Ledger.GetLivingHatchers() >= HatcherCapacity) ||
                    !FactoryLiveBudget::HasCreatureCapacity(LivingCost, OccupiedCost, Job.CreatureCost))
                    State = TEXT("mob_capacity");
                else if (Job.ConsecutiveFailures > 0) State = TEXT("placement");
                CreatureJobs.Add(MakeShared<FJsonValueObject>(MakeCreatureJobStatus(Job, State)));
            }
        }
        Body->SetArrayField(TEXT("creatureJobs"), MoveTemp(CreatureJobs));
        if (LastCreatureJobState)
        {
            Body->SetObjectField(TEXT("lastCreatureJob"), MakeCreatureJobStatus(LastCreatureJob, LastCreatureJobState));
        }
        Body->SetBoolField(TEXT("turnReady"), bTurnReady);
        Body->SetBoolField(TEXT("equipmentDropReady"), bEquipmentDropReady);
        Body->SetBoolField(TEXT("mixedCreatures"), true);
        TArray<TSharedPtr<FJsonValue>> AvailableCreatures;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(CreatureTypes); ++Index)
        {
            if (CreatureClasses[Index].IsValid()) AvailableCreatures.Add(MakeShared<FJsonValueString>(CreatureTypes[Index].Name));
        }
        Body->SetArrayField(TEXT("creatureTypes"), MoveTemp(AvailableCreatures));
        if (CreatureClasses[FindCreatureIndex(TEXT("crab-hatcher"))].IsValid() ||
            CreatureClasses[FindCreatureIndex(TEXT("elite-hatcher"))].IsValid())
        {
            const TSharedRef<FJsonObject> Hatchers = MakeShared<FJsonObject>();
            Hatchers->SetNumberField(TEXT("alive"), Ledger.GetLivingHatchers());
            Hatchers->SetNumberField(TEXT("pending"), Work.Hatchers);
            Hatchers->SetNumberField(TEXT("capacity"), HatcherCapacity);
            Body->SetObjectField(TEXT("hatcherTrial"), Hatchers);
        }
        TArray<TSharedPtr<FJsonValue>> Types;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(NobeliskTypes); ++Index)
        {
            if (NobeliskAmmo[Index].IsValid()) Types.Add(MakeShared<FJsonValueString>(NobeliskTypes[Index].Name));
        }
        if (HasFireworkTypes()) Body->SetBoolField(TEXT("nobeliskChaos"), true);
        Body->SetArrayField(TEXT("nobeliskTypes"), MoveTemp(Types));
        Body->SetNumberField(TEXT("nobeliskCapacity"), FactoryLiveBudget::MaxPendingNobelisks);
        const TSharedRef<FJsonObject> Drops = MakeShared<FJsonObject>();
        Drops->SetNumberField(TEXT("launched"), LaunchedNobelisks);
        Drops->SetNumberField(TEXT("impacted"), ImpactedNobelisks);
        Drops->SetNumberField(TEXT("expired"), ExpiredNobelisks);
        Drops->SetNumberField(TEXT("cancelled"), CancelledNobelisks);
        Drops->SetNumberField(TEXT("inFlight"), FallingNobelisks.Num());
        Body->SetObjectField(TEXT("nobelisks"), Drops);
        const TSharedRef<FJsonObject> Mobs = MakeShared<FJsonObject>();
        Mobs->SetNumberField(TEXT("spawned"), Ledger.GetSpawnedCount());
        Mobs->SetNumberField(TEXT("killed"), Ledger.GetKilledCount());
        Mobs->SetNumberField(TEXT("missing"), Ledger.GetMissingCount());
        Body->SetObjectField(TEXT("mobs"), Mobs);
        if (Backpack)
        {
            const FString Owner = Player ? AFactoryLiveBackpack::GetPlayerKey(*Player) : FString();
            const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetBoolField(TEXT("ready"), bBackpackReady);
            Data->SetBoolField(TEXT("snapshot"), Backpack->HasSnapshot(Owner));
            Data->SetBoolField(TEXT("recovery"), Backpack->HasRecovery(Owner));
            if (!LastBackpackId.IsEmpty())
            {
                const TSharedRef<FJsonObject> Last = MakeShared<FJsonObject>();
                Last->SetStringField(TEXT("id"), LastBackpackId);
                Last->SetStringField(TEXT("action"), LastBackpackAction);
                Last->SetStringField(TEXT("code"), LastBackpackCode);
                Last->SetNumberField(TEXT("stacks"), LastBackpackStacks);
                Data->SetObjectField(TEXT("last"), Last);
            }
            Body->SetObjectField(TEXT("backpack"), Data);
        }
        FString Payload;
        FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Payload));

        Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Endpoint);
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetHeader(TEXT("Authorization"), FString(TEXT("Bearer ")) + GameToken);
        Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
        Request->SetTimeout(1.5f);
        Request->SetContentAsString(Payload);
        Request->OnProcessRequestComplete().BindRaw(this, &FFactoryLiveModule::OnSync);
        if (!Request->ProcessRequest()) Request.Reset();
    }

    void OnSync(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
    {
        if (Completed != Request) return;
        Request.Reset();
        if (!bSuccess || !Response || Response->GetResponseCode() != 200)
        {
            if (!Response || Response->GetResponseCode() == 401) GameToken.Reset();
            return;
        }
        if (Response->GetContent().Num() > 4096) return;
        TSharedPtr<FJsonObject> Json;
        FString NewEpoch;
        bool bRemoteArmed = false;
        if (!ReadJson(Response->GetContentAsString(), Json) ||
            !Json->TryGetBoolField(TEXT("armed"), bRemoteArmed) ||
            !Json->TryGetStringField(TEXT("epoch"), NewEpoch) || NewEpoch.Len() != 36) return;

        LastSync = FPlatformTime::Seconds();
        if (NewEpoch != Epoch)
        {
            Disarm();
            LastAccepted.Reset();
            Epoch = MoveTemp(NewEpoch);
        }
        if (!bRemoteArmed && bArmed) Disarm();
        if (RespawnGuard.IsHolding(LastSync)) return;
        const TSharedPtr<FJsonObject>* BackpackCommand = nullptr;
        if (Json->TryGetObjectField(TEXT("backpackCommand"), BackpackCommand) && BackpackCommand &&
            BackpackCommand->IsValid() && !PendingBackpack)
        {
            FBackpackCommand Command;
            if ((*BackpackCommand)->TryGetStringField(TEXT("id"), Command.Id) && Command.Id.Len() == 36 &&
                Command.Id != LastBackpackId && (*BackpackCommand)->TryGetStringField(TEXT("action"), Command.Action) &&
                (Command.Action == TEXT("snapshot") || Command.Action == TEXT("restore") || Command.Action == TEXT("undo") ||
                    (bRemoteArmed && (Command.Action == TEXT("turn-around") || Command.Action == TEXT("equipment-drop")))))
            {
                Command.Expires = LastSync + BackpackCommandTimeout;
                PendingBackpack.Emplace(MoveTemp(Command));
            }
        }
        else if (!BackpackCommand)
        {
            PendingBackpack.Reset();
        }
        if (!bRemoteArmed) return;
        if (!bArmed) UE_LOG(LogFactoryLive, Display, TEXT("Stream effects armed"));
        bArmed = true;
        const TSharedPtr<FJsonObject>* Job = nullptr;
        if (Json->TryGetObjectField(TEXT("job"), Job) && Job && Job->IsValid())
        {
            ReceiveJob(**Job);
        }
    }

    void ReceiveJob(const FJsonObject& Json)
    {
        FJob Job;
        FString Effect;
        if (!Json.TryGetStringField(TEXT("id"), Job.Id) || Job.Id.Len() != 36 || Job.Id == LastAccepted ||
            !Json.TryGetStringField(TEXT("effect"), Effect) ||
            !Json.TryGetNumberField(TEXT("count"), Job.Remaining) || Job.Remaining < 1) return;
        if (Jobs.Num() >= MaxJobs) return;
        Job.Requested = Job.Remaining;
        const FPendingWork Work = GetPendingWork();
        if (Effect == TEXT("creatures"))
        {
            FString Creature;
            if (!Json.TryGetStringField(TEXT("creature"), Creature) ||
                Job.Remaining > FactoryLiveBudget::MaxPendingCreatures - Work.Creatures) return;
            Job.CreatureIndex = FindCreatureIndex(Creature);
            if (Job.CreatureIndex == INDEX_NONE || !CreatureClasses[Job.CreatureIndex].IsValid()) return;
            if (Job.IsHatcher())
            {
                const AFactoryLiveMobLedger* Ledger = AFactoryLiveMobLedger::Get(ActiveWorld.Get());
                if (!Ledger || Job.Remaining > HatcherCapacity - Ledger->GetLivingHatchers() - Work.Hatchers) return;
            }
            const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
            if (Json.TryGetArrayField(TEXT("variants"), Variants))
            {
                if (CreatureTypes[Job.CreatureIndex].Kind != ECreatureKind::Hostile || !Variants || Variants->IsEmpty() || Variants->Num() > 8) return;
                for (const TSharedPtr<FJsonValue>& Variant : *Variants)
                {
                    FString Name;
                    if (!Variant.IsValid() || !Variant->TryGetString(Name)) return;
                    const int32 Index = FindCreatureIndex(Name);
                    if (Index == INDEX_NONE || CreatureTypes[Index].Kind != ECreatureKind::Hostile || !CreatureClasses[Index].IsValid()) return;
                    Job.CreatureVariants.AddUnique(Index);
                }
                for (int32 Index = Job.CreatureVariants.Num() - 1; Index > 0; --Index)
                    Job.CreatureVariants.Swap(Index, FMath::RandRange(0, Index));
            }
            Job.CreatureCost = AFactoryLiveMobLedger::GetCreatureCost(FName(CreatureTypes[Job.CurrentCreatureIndex()].Name));
        }
        else
        {
            FString Variant;
            if (Effect == TEXT("cluster")) Variant = TEXT("cluster");
            else if (Effect != TEXT("nobelisk") || !Json.TryGetStringField(TEXT("nobelisk"), Variant)) return;
            if (Variant == TEXT("fireworks"))
            {
                if (Job.Remaining != FactoryLiveFireworks::DropCount || Work.Nobelisks != 0 || !HasFireworkTypes()) return;
                Job.bFireworks = true;
            }
            else
            {
                if (Work.bFireworks || Job.Remaining > FactoryLiveBudget::MaxPendingNobelisks - Work.Nobelisks) return;
                for (int32 Index = 0; Index < UE_ARRAY_COUNT(NobeliskTypes); ++Index)
                {
                    if (Variant == NobeliskTypes[Index].Name) Job.NobeliskIndex = Index;
                }
                if (!Job.IsNobelisk() || !NobeliskAmmo[Job.NobeliskIndex].IsValid()) return;
                if (Job.Remaining > NobeliskTypes[Job.NobeliskIndex].MaxPerJob) return;
                if (Job.IsNuke() && Work.Nukes > 0) return;
            }
        }
        Job.PlacementAngle = FMath::FRandRange(0.0f, 360.0f);
        Job.Expires = FPlatformTime::Seconds() + JobTimeout;
        LastAccepted = Job.Id;
        const TCHAR* Type = TEXT("fireworks");
        if (!Job.bFireworks) Type = Job.IsNobelisk() ? NobeliskTypes[Job.NobeliskIndex].Name : CreatureTypes[Job.CreatureIndex].Name;
        UE_LOG(LogFactoryLive, Display, TEXT("Accepted %s (%s, %s, %d)"), *Job.Id, *Effect, Type, Job.Remaining);
        Jobs.Add(MoveTemp(Job));
    }

    FPendingWork GetPendingWork() const
    {
        FPendingWork Work;
        Work.Nobelisks = FallingNobelisks.Num();
        for (const FFallingNobelisk& Drop : FallingNobelisks)
        {
            if (Drop.TypeIndex == NukeTypeIndex) ++Work.Nukes;
            Work.bFireworks |= Drop.bFireworks;
        }
        for (const FJob& Job : Jobs)
        {
            if (Job.IsNobelisk())
            {
                Work.Nobelisks += Job.Remaining;
                Work.bFireworks |= Job.bFireworks;
                if (Job.bFireworks) ++Work.Nukes;
                if (Job.IsNuke()) Work.Nukes += Job.Remaining;
            }
            else
            {
                Work.Creatures += Job.Remaining;
                if (Job.IsHatcher()) Work.Hatchers += Job.Remaining;
            }
        }
        return Work;
    }

    static TSharedRef<FJsonObject> MakeCreatureJobStatus(const FJob& Job, const TCHAR* State)
    {
        const TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
        Status->SetStringField(TEXT("id"), Job.Id);
        Status->SetStringField(TEXT("creature"), CreatureTypes[Job.CreatureIndex].Name);
        Status->SetStringField(TEXT("state"), State);
        Status->SetNumberField(TEXT("requested"), Job.Requested);
        Status->SetNumberField(TEXT("spawned"), Job.Requested - Job.Remaining);
        Status->SetNumberField(TEXT("remaining"), Job.Remaining);
        Status->SetNumberField(TEXT("consecutivePlacementFailures"), Job.ConsecutiveFailures);
        return Status;
    }

    void RecordCreatureJobResult(const FJob& Job, const TCHAR* State)
    {
        if (Job.IsNobelisk()) return;
        LastCreatureJob = Job;
        LastCreatureJobState = State;
    }

    void ProcessJobs(UWorld& World, AFGCharacterPlayer& Player, AFactoryLiveMobLedger& Ledger, double Now, bool bSlow)
    {
        for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
        {
            if (Now < Jobs[Index].Expires) continue;
            UE_LOG(LogFactoryLive, Warning, TEXT("Job %s timed out with %d remaining"), *Jobs[Index].Id, Jobs[Index].Remaining);
            RecordCreatureJobResult(Jobs[Index], TEXT("timed_out"));
            Jobs.RemoveAt(Index);
        }
        if (Jobs.IsEmpty()) return;
        const auto Ready = [&](const FJob& Job) { return Now >= Job.RetryAt; };
        int32 NobeliskIndex = INDEX_NONE;
        int32 CreatureIndex = INDEX_NONE;
        if (Now >= NextNobelisk)
        {
            NobeliskIndex = Jobs.IndexOfByPredicate([&](const FJob& Job) {
                if (!Job.IsNobelisk() || !Ready(Job)) return false;
                if (Job.bFireworks) return Job.Remaining > 1 || Now >= NextNobeliskType[NukeTypeIndex];
                return !bSlow && Now >= NextNobeliskType[Job.NobeliskIndex];
            });
            // Let a heavy drop at the head of the queue start after the standard drops land.
            if (NobeliskIndex != INDEX_NONE && !CanLaunchNobelisk(Jobs[NobeliskIndex]))
                NobeliskIndex = INDEX_NONE;
        }
        if ((NobeliskIndex == INDEX_NONE || bPreferCreature) && Now >= NextCreature)
        {
            // When the world is full, recheck at a fixed rate instead of querying AI each frame.
            NextCreature = Now + CreatureCapacityRecheck;
            const int32 OccupiedCost = Ledger.GetOccupiedCost();
            const int32 LivingCost = Ledger.GetLivingCost();
            const bool bHatcherSlotFree = Ledger.GetLivingHatchers() < HatcherCapacity;
            CreatureIndex = Jobs.IndexOfByPredicate([&](const FJob& Job) {
                return !Job.IsNobelisk() && Ready(Job) && (!Job.IsHatcher() || bHatcherSlotFree) &&
                    FactoryLiveBudget::HasCreatureCapacity(LivingCost, OccupiedCost, Job.CreatureCost);
            });
            if (CreatureIndex != INDEX_NONE)
            {
                const AFGCreatureSubsystem* Population = AFGCreatureSubsystem::Get(&World);
                bWorldCreatureCapacityBlocked = !Population ||
                    Population->GetNumActiveCreatures(true) >= FactoryLiveBudget::MaxWorldCreatures;
                if (bWorldCreatureCapacityBlocked) CreatureIndex = INDEX_NONE;
            }
        }
        const int32 Index = CreatureIndex == INDEX_NONE ? NobeliskIndex : CreatureIndex;
        if (Index == INDEX_NONE) return;

        FJob Job = MoveTemp(Jobs[Index]);
        Jobs.RemoveAt(Index);
        bPreferCreature = Job.IsNobelisk();
        const bool bSpawned = Job.IsNobelisk()
            ? SpawnNobelisk(World, Player, Job, Now)
            : SpawnCreature(World, Player, Ledger, Job);
        ++Job.PlacementIndex;
        const double Finished = FPlatformTime::Seconds();
        FrameBudget.ReportWork(Finished, Finished - Now);
        if (bSpawned)
        {
            --Job.Remaining;
            Job.ConsecutiveFailures = 0;
            if (!Job.CreatureVariants.IsEmpty() && Job.Remaining > 0)
                Job.CreatureCost = AFactoryLiveMobLedger::GetCreatureCost(FName(CreatureTypes[Job.CurrentCreatureIndex()].Name));
        }
        else
        {
            ++Job.ConsecutiveFailures;
            Job.RetryAt = Finished + FrameBudget.PlacementRetryDelay(Job.ConsecutiveFailures);
        }
        if (Job.bFireworks)
        {
            const int32 Launched = Job.Requested - Job.Remaining;
            const double Interval = bSpawned ? FactoryLiveFireworks::DelayAfterLaunch(Launched) : FactoryLiveFireworks::LaunchInterval;
            NextNobelisk = Finished + FrameBudget.SpawnDelay(Interval);
        }
        else if (Job.IsNobelisk()) NextNobelisk = Finished + NobeliskLaunchInterval;
        else NextCreature = Finished + FrameBudget.SpawnDelay(FactoryLiveBudget::CreatureSpawnInterval);
        if (Job.ConsecutiveFailures >= MaxPlacementFailures)
        {
            RecordCreatureJobResult(Job, TEXT("placement_failed"));
            UE_LOG(LogFactoryLive, Warning, TEXT("Job %s stopped after %d failed placements (%d of %d remaining)"),
                *Job.Id, Job.ConsecutiveFailures, Job.Remaining, Job.Requested);
        }
        else if (Job.Remaining > 0)
        {
            Jobs.Add(MoveTemp(Job));
        }
        else
        {
            RecordCreatureJobResult(Job, TEXT("completed"));
            UE_LOG(LogFactoryLive, Display, TEXT("Finished spawning for job %s"), *Job.Id);
        }
    }

    static bool TraceCreatureFloor(UWorld& World, const FVector& Candidate, double PlayerFloorZ, float WalkableFloorZ,
        ECollisionChannel Channel, const FCollisionQueryParams& Query, const FCollisionResponseParams& Response, FVector& Floor)
    {
        const FVector Top(Candidate.X, Candidate.Y, PlayerFloorZ + CreatureGroundProbeUp);
        const FVector Bottom(Candidate.X, Candidate.Y, PlayerFloorZ - MaxCreatureLandingDrop);
        FHitResult Hit;
        if (!World.LineTraceSingleByChannel(Hit, Top, Bottom, Channel, Query, Response) ||
            !Hit.IsValidBlockingHit() || Cast<APawn>(Hit.GetActor()) || Hit.ImpactNormal.Z < WalkableFloorZ) return false;
        Floor = Hit.ImpactPoint;
        return true;
    }

    static bool SpawnHatcher(UWorld& World, AFGCharacterPlayer& Player, AFactoryLiveMobLedger& Ledger, const FJob& Job, UClass* Class)
    {
        const UCapsuleComponent* Capsule = Player.GetCapsuleComponent();
        const FVector Origin = Player.GetActorLocation();
        const double PlayerFloorZ = Origin.Z - Capsule->GetScaledCapsuleHalfHeight();
        const double Radius = 600.0 + (Job.PlacementIndex / 2 % 16) * 400.0;
        const FVector Candidate = Origin + PlacementDirection(Job) * Radius;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryLiveHatcherLanding), false, &Player);
        const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
        FVector Floor;
        if (!TraceCreatureFloor(World, Candidate, PlayerFloorZ, Player.GetCharacterMovement()->GetWalkableFloorZ(),
            Capsule->GetCollisionObjectType(), Query, Response, Floor)) return false;

        // Hatchers are stationary actors with a different root, so keep their spawn close to the floor.
        const double Clearance = 12.0 + (Job.ConsecutiveFailures / 4 % 3) * 30.0;
        const FVector Position = Floor + FVector(0, 0, Clearance);
        const FTransform Transform(FRotator(0, (Origin - Position).Rotation().Yaw, 0), Position);
        FActorSpawnParameters Params;
        Params.OverrideLevel = World.PersistentLevel;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        Params.bDeferConstruction = true;
        AFGCrabHatcher* Hatcher = World.SpawnActor<AFGCrabHatcher>(Class, Transform, Params);
        if (!IsValid(Hatcher)) return false;
        Hatcher->FinishSpawning(Transform);
        if (!IsValid(Hatcher)) return false;
        const FVector Adjusted = Hatcher->GetActorLocation();
        if (FVector::DistSquared2D(Adjusted, Floor) > FMath::Square(150.0) ||
            Adjusted.Z < Floor.Z - 30.0 || Adjusted.Z > Floor.Z + 180.0 ||
            !Ledger.Track(*Hatcher, FName(CreatureTypes[Job.CreatureIndex].Name), Job.Id))
        {
            Hatcher->Destroy();
            return false;
        }
        return true;
    }

    bool SpawnCreature(UWorld& World, AFGCharacterPlayer& Player, AFactoryLiveMobLedger& Ledger, FJob& Job)
    {
        const int32 CreatureIndex = Job.CurrentCreatureIndex();
        UClass* Class = CreatureClasses[CreatureIndex].Get();
        if (!Class) return false;
        if (Job.IsHatcher()) return SpawnHatcher(World, Player, Ledger, Job, Class);
        const FVector Origin = Player.GetActorLocation();
        const ECreatureKind Kind = CreatureTypes[CreatureIndex].Kind;
        if (Kind == ECreatureKind::FlyingCrab || Kind == ECreatureKind::Manta)
        {
            const bool bManta = Kind == ECreatureKind::Manta;
            const FVector Heading = PlacementDirection(Job);
            const FVector At = Origin + Heading * (bManta ? 5500.0 : 950.0) + FVector(0, 0, bManta ? 7000.0 : 180.0);
            const FTransform Home((-Heading).Rotation(), At);
            AFGSplinePath* Route = nullptr;
            AActor* Actor = FactoryLiveFliers::Spawn(World, Class, Home, At, Player, Route);
            if (!Actor) return false;
            Ledger.TrackFlier(*Actor, Route, Home, FName(CreatureTypes[CreatureIndex].Name), Job.Id);
            return true;
        }
        const double PlayerFloorZ = Origin.Z - Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        // Try two headings at each distance before moving farther out.
        const bool bTryPreferredPlacement = Job.ConsecutiveFailures % 2 == 0;
        const bool bTryGround = Job.bPreferGroundPlacement == bTryPreferredPlacement;
        auto* Navigation = bTryGround ? nullptr : FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
        const double Radius = 900.0 + (Job.PlacementIndex / 2 % 16) * 480.0;
        const FVector Candidate = Origin + PlacementDirection(Job) * Radius;
        const AFGCreature* Defaults = Class->GetDefaultObject<AFGCreature>();
        const UCapsuleComponent* Capsule = Defaults->GetCapsuleComponent();
        if (!Capsule) return false;

        const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryLiveCreatureLanding), false, &Player);
        const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
        const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
        FVector Floor;
        if (!Navigation)
        {
            if (!TraceCreatureFloor(World, Candidate, PlayerFloorZ, Defaults->GetCharacterMovement()->GetWalkableFloorZ(),
                Channel, Query, Response, Floor)) return false;
        }
        else
        {
            FNavLocation NavFloor;
            const FNavAgentProperties& NavAgent = Defaults->GetNavAgentPropertiesRef();
            if (!Navigation->ProjectPointToNavigation(Candidate, NavFloor, FVector(150, 150, 550), &NavAgent)) return false;
            Floor = NavFloor.Location;
        }
        if (Floor.Z < PlayerFloorZ - MaxCreatureLandingDrop) return false;
        const FVector Landing = Floor + FVector(0, 0, HalfHeight + 12.0);
        FVector Position = Landing;
        Position.Z = FMath::Max(Landing.Z, Origin.Z + CreatureHeightAbovePlayer);
        const double Drop = Position.Z - Landing.Z;
        if (Drop > MaxCreatureLandingDrop ||
            (Drop > 0.0 && World.SweepTestByChannel(Position, Landing, FQuat::Identity, Channel, Shape, Query, Response)))
            Position = Landing;
        if (Position.Z == Landing.Z &&
            World.OverlapBlockingTestByChannel(Landing, FQuat::Identity, Channel, Shape, Query, Response)) return false;
        const FRotator Rotation(0, (Origin - Position).Rotation().Yaw, 0);
        const FTransform SpawnTransform(Rotation, Position);
        FActorSpawnParameters Params;
        Params.OverrideLevel = World.PersistentLevel;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        Params.bDeferConstruction = true;
        AFGCreature* Creature = World.SpawnActor<AFGCreature>(Class, SpawnTransform, Params);
        if (!IsValid(Creature)) return false;
        Creature->FinishSpawning(SpawnTransform);
        if (!IsValid(Creature)) return false;
        if (!Ledger.Track(*Creature, FName(CreatureTypes[CreatureIndex].Name), Job.Id))
        {
            const TWeakObjectPtr<AController> SpawnedController = Creature->GetController();
            if (Creature->Destroy())
            {
                if (AController* AI = SpawnedController.Get(); AI && !AI->IsPlayerController()) AI->Destroy();
            }
            return false;
        }
        if (!Creature->GetController()) Creature->SpawnDefaultController();
        Job.bPreferGroundPlacement = !Navigation;
        return true;
    }

    static FVector PlacementDirection(const FJob& Job)
    {
        const double Angle = FMath::DegreesToRadians(Job.PlacementAngle + Job.PlacementIndex * 137.50776);
        return FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
    }

    bool HasFireworkTypes() const
    {
        if (!ExplodedFlag) return false;
        for (const auto& Ammo : NobeliskAmmo)
        {
            if (!Ammo.IsValid()) return false;
        }
        return true;
    }

    bool CanLaunchNobelisk(const FJob& Job) const
    {
        if (FallingNobelisks.IsEmpty()) return true;
        if (Job.bFireworks)
            return Job.Remaining > 1 && FallingNobelisks.Num() < FactoryLiveFireworks::MaxInFlight;
        return FallingNobelisks.Num() < FactoryLiveBudget::MaxFlyingNobelisks &&
            !NobeliskTypes[Job.NobeliskIndex].bExclusive && !NobeliskTypes[FallingNobelisks[0].TypeIndex].bExclusive;
    }

    bool SpawnNobelisk(UWorld& World, AFGCharacterPlayer& Player, FJob& Job, double Now)
    {
        int32 TypeIndex = Job.NobeliskIndex;
        if (Job.bFireworks) TypeIndex = Job.Remaining == 1 ? NukeTypeIndex : FMath::RandHelper(NukeTypeIndex);
        UFGAmmoTypeProjectile* Descriptor = NobeliskAmmo[TypeIndex].Get();
        UClass* ProjectileClass = Descriptor ? Descriptor->GetProjectileClass().Get() : nullptr;
        if (!ProjectileClass) return false;
        const AFGStickyExplosive* Defaults = ProjectileClass->GetDefaultObject<AFGStickyExplosive>();
        const USphereComponent* DefaultSphere = Defaults->GetCollisionComp();
        if (!DefaultSphere) return false;

        const FVector Direction = PlacementDirection(Job);
        const double Height = 610.0 - (Job.PlacementIndex % 4) * 70.0;
        FVector Position = Player.GetActorLocation() + Direction * 310.0 + FVector(0, 0, Height);
        FVector Velocity = -Direction * 225.0 + FVector(0, 0, -375.0);
        FRotator Rotation = (-Direction).Rotation();
        if (Job.bFireworks)
        {
            const int32 Launched = Job.Requested - Job.Remaining;
            if (Launched % FactoryLiveFireworks::DropsPerStar == 0 && Job.ConsecutiveFailures == 0)
                Job.FireworkOrigin = Player.GetActorLocation();
            const double StarAngle = FMath::DegreesToRadians(Job.PlacementAngle + Job.ConsecutiveFailures * 7.0);
            const auto Offset = FactoryLiveFireworks::StarOffset(Launched, StarAngle);
            const FVector Radial(Offset.X, Offset.Y, 0.0);
            const double FireworkHeight = 500.0 + (Launched % 5) * 25.0 + (Job.ConsecutiveFailures % 3) * 120.0;
            Position = Job.FireworkOrigin + Radial * 0.65 + FVector(0, 0, FireworkHeight);
            Velocity = Radial * 0.52 + FVector(0, 0, -475.0);
            Rotation = Radial.Rotation();
        }

        FCollisionResponseContainer Blocking = DefaultSphere->GetCollisionResponseToChannels();
        for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody, ECC_Vehicle, ECC_Destructible})
        {
            Blocking.SetResponse(Channel, ECR_Block);
        }
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryLiveNobeliskDrop), false);
        if (World.OverlapBlockingTestByChannel(Position, FQuat::Identity, DefaultSphere->GetCollisionObjectType(),
            FCollisionShape::MakeSphere(DefaultSphere->GetScaledSphereRadius()), Query, FCollisionResponseParams(Blocking)))
        {
            return false;
        }

        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        AFGStickyExplosive* Bomb = World.SpawnActor<AFGStickyExplosive>(ProjectileClass, Position, Rotation, Params);
        if (!IsValid(Bomb)) return false;
        Bomb->SetSourceAmmoDescriptor(Descriptor);
        USphereComponent* Sphere = Bomb->GetCollisionComp();
        UFGProjectileMovementComponent* Movement = Bomb->GetProjectileMovement();
        if (!Sphere || !Movement)
        {
            Bomb->Destroy();
            return false;
        }

        Bomb->SetCanBeDamaged(false);
        Sphere->SetCollisionResponseToChannels(Blocking);
        for (const FFallingNobelisk& Drop : FallingNobelisks)
        {
            if (AFGStickyExplosive* Other = Drop.Actor.Get())
            {
                Sphere->IgnoreActorWhenMoving(Other, true);
                Other->GetCollisionComp()->IgnoreActorWhenMoving(Bomb, true);
            }
        }
        TStrongObjectPtr<UFactoryLiveNobeliskImpact> Impact(NewObject<UFactoryLiveNobeliskImpact>(Bomb));
        Sphere->OnComponentHit.AddDynamic(Impact.Get(), &UFactoryLiveNobeliskImpact::OnComponentHit);
        Movement->OnProjectileStop.AddDynamic(Impact.Get(), &UFactoryLiveNobeliskImpact::OnProjectileStop);
        Movement->bShouldBounce = false;
        Movement->bSweepCollision = true;
        Movement->ProjectileGravityScale = 1.0f;
        Bomb->SetInitialVelocity(Velocity);
        Bomb->SetAndEnableRotation(1.0f);
        const bool bMoreNukesInWave = Job.IsNuke() && Job.Remaining > 1;
        if (bMoreNukesInWave) NextNobeliskType[TypeIndex] = FPlatformTime::Seconds() + NukeWaveLaunchInterval;
        FallingNobelisks.Add({Bomb, MoveTemp(Impact), TypeIndex, Now + NobeliskFlightTimeout, bMoreNukesInWave, Job.bFireworks});
        ++LaunchedNobelisks;
        return true;
    }

    FDelegateHandle TickHandle;
    FDelegateHandle CleanupHandle;
    IConsoleCommand* MobCommand = nullptr;
    TWeakObjectPtr<UWorld> ActiveWorld;
    TArray<FJob> Jobs;
    TArray<FFallingNobelisk, TInlineAllocator<FactoryLiveFireworks::MaxInFlight>> FallingNobelisks;
    TSharedPtr<FStreamableHandle> AssetLoad;
    TWeakObjectPtr<UClass> CreatureClasses[UE_ARRAY_COUNT(CreatureTypes)];
    TWeakObjectPtr<UFGAmmoTypeProjectile> NobeliskAmmo[UE_ARRAY_COUNT(NobeliskTypes)];
    const FBoolProperty* ExplodedFlag = nullptr;
    FHttpRequestPtr Request;
    FProcHandle BundledBridge;
    FString BundledBridgeDirectory;
    bool bBundledBridgeAttempted = false;
    FString LinkPath;
    FString Endpoint;
    FString GameToken;
    FString Session;
    FString Epoch;
    FString LastAccepted;
    TOptional<FBackpackCommand> PendingBackpack;
    FString LastBackpackId;
    FString LastBackpackAction;
    FString LastBackpackCode;
    int32 LastBackpackStacks = 0;
    FJob LastCreatureJob;
    const TCHAR* LastCreatureJobState = nullptr;
    bool bWorldCreatureCapacityBlocked = false;
    bool bCancelFallingDrops = false;
    bool bArmed = false;
    bool bCheckedAssets = false;
    bool bAssetsReady = false;
    FactoryLiveBudget::FFrameBudget FrameBudget;
    FactoryLiveBudget::FRespawnGuard RespawnGuard;
    int32 LaunchedNobelisks = 0;
    int32 ImpactedNobelisks = 0;
    int32 ExpiredNobelisks = 0;
    int32 CancelledNobelisks = 0;
    double LastSync = 0;
    double NextSync = 0;
    double NextCreature = 0;
    bool bPreferCreature = true;
    double NextNobelisk = 0;
    double NextFireworkImpact = 0;
    double NextEquipmentDrop = 0;
    double NextTurnAround = 0;
    double NextNobeliskType[UE_ARRAY_COUNT(NobeliskTypes)] = {};
};

IMPLEMENT_MODULE(FFactoryLiveModule, FactoryLive)
