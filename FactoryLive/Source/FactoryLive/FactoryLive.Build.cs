using UnrealBuildTool;

public class FactoryLive : ModuleRules
{
    public FactoryLive(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreOnline", "CoreUObject", "Engine", "FactoryGame", "SML", "HTTP", "Json", "NavigationSystem", "PhysicsCore", "Projects"
        });
    }
}
