// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class HandlingRagdolls : ModuleRules
{
	public HandlingRagdolls(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore",
			"PhysicsCore",
			"HeadMountedDisplay",
			"EnhancedInput"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AnimGraphRuntime"
		});
	}
}
