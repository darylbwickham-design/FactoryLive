#pragma once

#include "CoreMinimal.h"

class AFGCharacterPlayer;
class AFGSplinePath;

namespace FactoryLiveFliers
{
    AActor* Spawn(UWorld& World, UClass* Class, const FTransform& Home, const FVector& Position,
        AFGCharacterPlayer& Player, AFGSplinePath*& Route);
}
