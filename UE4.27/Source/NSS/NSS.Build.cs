// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System;
using System.IO;

public class NSS : ModuleRules
{
	public NSS(ReadOnlyTargetRules Target) : base(Target)
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
				"Engine",
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
				"Landscape",
				"CoreUObject",
				"NGSettings",
				"NGVulkanBackend",
			}
		);

		PrecompileForTargets = PrecompileTargetsType.Any;
	}
}
