// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MenuSystem : ModuleRules
{
	public MenuSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"OnlineSubsystemSteam",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"MenuSystem",
			"MenuSystem/Variant_Platforming",
			"MenuSystem/Variant_Platforming/Animation",
			"MenuSystem/Variant_Combat",
			"MenuSystem/Variant_Combat/AI",
			"MenuSystem/Variant_Combat/Animation",
			"MenuSystem/Variant_Combat/Gameplay",
			"MenuSystem/Variant_Combat/Interfaces",
			"MenuSystem/Variant_Combat/UI",
			"MenuSystem/Variant_SideScrolling",
			"MenuSystem/Variant_SideScrolling/AI",
			"MenuSystem/Variant_SideScrolling/Gameplay",
			"MenuSystem/Variant_SideScrolling/Interfaces",
			"MenuSystem/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
