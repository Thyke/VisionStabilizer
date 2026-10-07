using UnrealBuildTool;

public class VisionStabilizerEditor : ModuleRules
{
    public VisionStabilizerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "AnimGraph", "BlueprintGraph", "VisionStabilizer"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd", "AnimGraphRuntime", "KismetCompiler", "PropertyEditor",
            "Slate", "SlateCore", "AssetRegistry"
        });
    }
}
