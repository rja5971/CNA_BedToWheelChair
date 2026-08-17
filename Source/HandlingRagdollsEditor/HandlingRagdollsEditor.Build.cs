using UnrealBuildTool;
using System.IO;

public class HandlingRagdollsEditor : ModuleRules
{
	public HandlingRagdollsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
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
			"AssetRegistry"
		});

		// Include runtime module headers
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../HandlingRagdolls"));
	}
}
