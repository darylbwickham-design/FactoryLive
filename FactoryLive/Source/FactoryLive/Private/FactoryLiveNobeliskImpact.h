#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "FactoryLiveNobeliskImpact.generated.h"

class UPrimitiveComponent;

// Movement broadcasts during its sweep. Record the impact and detonate from the world tick.
UCLASS()
class UFactoryLiveNobeliskImpact final : public UObject
{
    GENERATED_BODY()

public:
    bool HasImpact() const { return bImpacted; }

    UFUNCTION()
    void OnProjectileStop(const FHitResult& Hit)
    {
        bImpacted |= Hit.bBlockingHit;
    }

    UFUNCTION()
    void OnComponentHit(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse, const FHitResult& Hit)
    {
        OnProjectileStop(Hit);
    }

private:
    bool bImpacted = false;
};
