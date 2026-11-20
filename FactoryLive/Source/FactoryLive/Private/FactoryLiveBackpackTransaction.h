#pragma once

enum class EFactoryLiveBackpackWrite
{
    Applied,
    RolledBack,
    RecoveryRequired
};

template<typename Slots, typename WriteAndVerify>
EFactoryLiveBackpackWrite ReplaceBackpackSlots(const Slots& Before, const Slots& Desired, WriteAndVerify Write)
{
    if (Write(Desired)) return EFactoryLiveBackpackWrite::Applied;
    if (Write(Before)) return EFactoryLiveBackpackWrite::RolledBack;
    return EFactoryLiveBackpackWrite::RecoveryRequired;
}
