using UnrealBuildTool;

public class VisionStabilizer : ModuleRules
{
    public VisionStabilizer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "AnimGraphRuntime"
        });
    }
}
