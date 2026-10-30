// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (c) 2023-2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.IO;
using System;
using System.Diagnostics;

public class NGVulkanBackend : ModuleRules
{
	public NGVulkanBackend(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp17;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI",
				"NGShared",
				"VulkanRHI",
			}
		);

		PublicIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory, "../NG-SDK/sdk/include"),
				Path.Combine(ModuleDirectory, "../NG-SDK/ffx-api/include/ffx_api"),
				Path.Combine(ModuleDirectory, "../NG-SDK/ffx-api/include/ffx_api/vk"),
				Path.Combine(ModuleDirectory, "../NG-SDK/sdk/include/FidelityFX/host/backends/vk")
			}
			);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) ||
			Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ||
			Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
			PrivateIncludePaths.AddRange(
				new string[]
				{
					Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private"),
				}
			);

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
				PrivateIncludePaths.Add(Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private/Windows"));
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
				PrivateIncludePaths.Add(Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private/Android"));
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
				{
					PrivateDependencyModuleNames.Add("ApplicationCore");
					AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");

					string VulkanSDKPath = Environment.GetEnvironmentVariable("VULKAN_SDK");
					bool bSDKInstalled = !String.IsNullOrEmpty(VulkanSDKPath);
					if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux || !bSDKInstalled)
					{
						AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
					}
					else
					{
						PrivateIncludePaths.Add(VulkanSDKPath + "/include");
						PrivateIncludePaths.Add(VulkanSDKPath + "/include/vulkan");
						PublicAdditionalLibraries.Add(Path.Combine(VulkanSDKPath, "lib", "libvulkan.so"));
					}

					PrivateIncludePaths.Add(Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private/Linux"));
				}
				else
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, "VkHeadersExternal");
					PrivateIncludePaths.Add(
						Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private/" + Target.Platform));
				}
			}
			else
			{
				PrivateIncludePaths.Add(
					Path.Combine(EngineDir, @"Source/Runtime/VulkanRHI/Private/" + Target.Platform));
			}

			PrecompileForTargets = PrecompileTargetsType.Any;
		}
		else
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		string PrebuiltPath = Path.Combine(ModuleDirectory, "../NG-SDK/prebuilt_binaries/");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows x64
			string LibName = "ngsdk_windows_x64";
			string DllInPrebuilt = Path.Combine(PrebuiltPath, LibName + ".dll");
			string LibInPrebuilt = Path.Combine(PrebuiltPath, LibName + ".lib");

			if (!File.Exists(DllInPrebuilt))
			{
				throw new BuildException("[NGVulkanBackend] Missing " + LibName + ".dll.\nChecked:\n  " + DllInPrebuilt);
			}
			PublicDelayLoadDLLs.Add(LibName + ".dll");

			if (!File.Exists(LibInPrebuilt))
			{
				throw new BuildException("[NGVulkanBackend] Missing " + LibName + ".lib.\nChecked:\n  " + LibInPrebuilt);
			}
			PublicAdditionalLibraries.Add(LibInPrebuilt);

			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", LibName + ".dll"), DllInPrebuilt);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			System.Console.WriteLine("Architecture: " + Target.Architecture);

			string LibName = Target.Architecture == "arm64" ? "libngsdk_linux_arm.so" :
				"libngsdk_linux_x64.so";
			PublicAdditionalLibraries.Add(Path.Combine(PrebuiltPath, LibName));
			PublicDelayLoadDLLs.Add(LibName);
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", LibName), Path.Combine(PrebuiltPath, LibName));
		}
	}
}
