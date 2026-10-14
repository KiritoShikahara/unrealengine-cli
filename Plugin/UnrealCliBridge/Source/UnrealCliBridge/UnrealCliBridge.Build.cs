using UnrealBuildTool;

public class UnrealCliBridge : ModuleRules
{
    public UnrealCliBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Sockets", "Networking",
            "Json", "JsonUtilities", "Projects", "BlueprintGraph", "Kismet", "KismetCompiler",
            "AssetRegistry", "AssetTools", "LevelEditor", "PythonScriptPlugin", "Slate", "SlateCore",
            "EditorSubsystem", "ToolMenus"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("LiveCoding");
            PrivateDefinitions.Add("UCLI_WITH_LIVE_CODING=1");
        }
        else
        {
            PrivateDefinitions.Add("UCLI_WITH_LIVE_CODING=0");
        }
    }
}
