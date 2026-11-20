#include "FactoryLiveFliers.h"
#include "Components/SplineComponent.h"
#include "Creature/Enemy/FGFlyingBabyCrab.h"
#include "Engine/World.h"
#include "FGCharacterPlayer.h"
#include "FGManta.h"
#include "FGSplinePath.h"
#include "UObject/UnrealType.h"

AActor* FactoryLiveFliers::Spawn(UWorld& World, UClass* Class, const FTransform& Home,
    const FVector& Position, AFGCharacterPlayer& Player, AFGSplinePath*& Route)
{
    Route = nullptr;
    if (!Class) return nullptr;
    FActorSpawnParameters Params;
    Params.OverrideLevel = World.PersistentLevel;
    Params.ObjectFlags |= RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    if (Class->IsChildOf(AFGFlyingBabyCrab::StaticClass()))
    {
        auto* Crab = World.SpawnActor<AFGFlyingBabyCrab>(Class, Position, Home.Rotator(), Params);
        if (!IsValid(Crab)) return nullptr;
        Crab->SetHomeLocation(Home.GetLocation());
        Crab->RegisterHostilePlayer(&Player);
        return Crab;
    }
    if (!Class->IsChildOf(AFGManta::StaticClass())) return nullptr;
    // Vanilla assigns each manta a level spline before BeginPlay. Give a redeemed
    // manta its own local loop, leaving both of the world's authored routes alone.
    const auto* PathProperty = FindFProperty<FObjectPropertyBase>(Class, TEXT("mSplinePath"));
    if (!PathProperty || !AFGSplinePath::StaticClass()->IsChildOf(PathProperty->PropertyClass)) return nullptr;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Route = World.SpawnActor<AFGSplinePath>(AFGSplinePath::StaticClass(), Home, Params);
    if (!IsValid(Route)) return nullptr;
    if (!IsValid(Route->mSpline))
    {
        Route->mSpline = NewObject<USplineComponent>(Route);
        Route->AddInstanceComponent(Route->mSpline);
        Route->SetRootComponent(Route->mSpline);
        Route->mSpline->RegisterComponent();
    }
    const TArray<FVector> Loop = {
        Home.GetLocation(), Home.TransformPosition(FVector(11000, 9500, 1000)),
        Home.TransformPosition(FVector(3000, 20000, 1800)), Home.TransformPosition(FVector(-10000, 10500, 500))
    };
    Route->mSpline->SetSplinePoints(Loop, ESplineCoordinateSpace::World, false);
    Route->mSpline->SetClosedLoop(true);
    Params.bDeferConstruction = true;
    const FTransform SpawnAt(Home.GetRotation(), Home.GetLocation());
    auto* Manta = World.SpawnActor<AFGManta>(Class, SpawnAt, Params);
    if (IsValid(Manta))
    {
        PathProperty->SetObjectPropertyValue_InContainer(Manta, Route);
        Manta->FinishSpawning(SpawnAt);
        if (IsValid(Manta)) return Manta;
    }
    Route->Destroy();
    Route = nullptr;
    return nullptr;
}
