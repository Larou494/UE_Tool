using UnrealBuildTool;

public class MaterialParentBatch : ModuleRules
{
    public MaterialParentBatch(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "MaterialEditor",
            "Slate", "SlateCore", "InputCore", "ToolMenus", "ContentBrowser",
            "AssetRegistry", "AssetTools", "PropertyEditor", "Json", "JsonUtilities", "RHI", "RenderCore"
        });
    }
}
