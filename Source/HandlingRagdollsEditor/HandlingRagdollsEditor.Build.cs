using UnrealBuildTool;
using System.IO;

public class HandlingRagdollsEditor : ModuleRules
{
	public HandlingRagdollsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		bEnableExceptions = true;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"PropertyEditor",
			"EditorStyle",
			"AssetTools",
			"ContentBrowser",
			"HandlingRagdolls"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"AssetRegistry",
			"BlueprintGraph",
			"InputBlueprintNodes",
			"Kismet",
			"KismetCompiler",
			"UMG",
			"EnhancedInput",
			"MediaAssets"
		});

		// Include runtime module headers
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../HandlingRagdolls"));
	}
}
