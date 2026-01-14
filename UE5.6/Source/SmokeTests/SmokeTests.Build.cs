//
// Copyright © 2025 Arm Limited.
// SPDX-License-Identifier: MIT
//

using UnrealBuildTool;

public class SmokeTests : ModuleRules
{
	public SmokeTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(new string[] { "SmokeTests/Private" });

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Projects",
			"AutomationTest",
			"RenderCore",
			"NGSettings"
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}