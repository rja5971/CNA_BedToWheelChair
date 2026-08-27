using UnrealBuildTool;

public class CNABedToWheelchairEditorTarget : TargetRules
{
	public CNABedToWheelchairEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		ExtraModuleNames.AddRange(new string[] { "HandlingRagdolls", "HandlingRagdollsEditor" });
	}
}
