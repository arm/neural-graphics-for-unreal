// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System;
using System.IO;

public class NGSettings : ModuleRules
{
	public NGSettings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;


		PublicIncludePaths.AddRange(
			new string[] {
				EngineDirectory + "/Source/Runtime/Renderer/Private",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"NGShared"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI",
				"CoreUObject",
				"EngineSettings",
				"DeveloperSettings",
			}
		);

		PrecompileForTargets = PrecompileTargetsType.Any;
	}
}
