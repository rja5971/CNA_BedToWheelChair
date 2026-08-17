using UnrealBuildTool;

public class CNABedToWheelchairTarget : TargetRules
{
	public CNABedToWheelchairTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		ExtraModuleNames.AddRange(new string[] { "HandlingRagdolls" });
	}
}
