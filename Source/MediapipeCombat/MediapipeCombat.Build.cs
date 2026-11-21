// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MediapipeCombat : ModuleRules
{
	public MediapipeCombat(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "Networking", "Sockets", "SlateCore" });

		PublicIncludePaths.AddRange(new string[] {
			"MediapipeCombat",
			"MediapipeCombat/Variant_Platforming",
			"MediapipeCombat/Variant_Platforming/Animation",
			"MediapipeCombat/Variant_Combat",
			"MediapipeCombat/Variant_Combat/AI",
			"MediapipeCombat/Variant_Combat/Animation",
			"MediapipeCombat/Variant_Combat/Gameplay",
			"MediapipeCombat/Variant_Combat/Interfaces",
			"MediapipeCombat/Variant_Combat/UI",
			"MediapipeCombat/Variant_SideScrolling",
			"MediapipeCombat/Variant_SideScrolling/AI",
			"MediapipeCombat/Variant_SideScrolling/Gameplay",
			"MediapipeCombat/Variant_SideScrolling/Interfaces",
			"MediapipeCombat/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
