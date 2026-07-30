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

// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.IO;
using System;
using System.Diagnostics;
using EpicGames.Core;

public class NGVulkanBackend : ModuleRules
{
	public NGVulkanBackend(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI",
				"RHICore",
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
			PublicIncludePaths.AddRange(
				new string[]
				{
					Path.Combine(EngineDir, @"Source/Runtime/Renderer/Private"),
					Path.Combine(EngineDir, @"Source/Runtime/Renderer/Internal"),
				}
			);

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

		string PrebuiltPath = Path.Combine(ModuleDirectory, "../../Binaries/ThirdParty/PreBuiltNGSDK/");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows x64
			string LibName = "ngsdk_windows_x64";
			string DllInPrebuilt = Path.Combine(PrebuiltPath, LibName + ".dll");
			string LibInPrebuilt = Path.Combine(PrebuiltPath, LibName + ".lib");

			if (!File.Exists(DllInPrebuilt) || !File.Exists(LibInPrebuilt))
			{
				RunBuildSDK("vk_windows_x64");
			}

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

			AddPrebuiltNGLayer(PrebuiltPath, subDir: null,
				layerFileName: "VkLayer_arm_NG.dll",
				includeJsonManifest: true,
				requireExists: false);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			string LibName =
				Target.Architecture == UnrealArch.Arm64 ? "libngsdk_linux_arm.so" :
				"libngsdk_linux_x64.so";
			PublicAdditionalLibraries.Add(Path.Combine(PrebuiltPath, LibName));
			PublicDelayLoadDLLs.Add(LibName);
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", LibName), Path.Combine(PrebuiltPath, LibName));

			string LayerSubDir = Target.Architecture == UnrealArch.Arm64 ? "LinuxArm64" : "Linux";
			AddPrebuiltNGLayer(PrebuiltPath, subDir: LayerSubDir,
				layerFileName: "libVkLayer_arm_NG.so",
				includeJsonManifest: true,
				requireExists: false);
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string LibName = "libngsdk_android_arm.so";

			if (!File.Exists(Path.Combine(PrebuiltPath, LibName)))
			{
				RunBuildSDK("vk_android_arm");
			}

			PublicAdditionalLibraries.Add(Path.Combine(PrebuiltPath, LibName));
			PublicDelayLoadDLLs.Add(LibName);
			RuntimeDependencies.Add($"$(TargetOutputDir)/{LibName}", Path.Combine(PrebuiltPath, LibName));

			AddPrebuiltNGLayer(PrebuiltPath, subDir: null,
				layerFileName: "libVkLayer_arm_NG.so",
				includeJsonManifest: false);

			string AplPath = "NGVulkanBackend_APL.xml";
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModuleDirectory, AplPath));
		}
	}

	// Run BuildSDK.py with the specified build target to produce prebuilt binaries.
	private void RunBuildSDK(string buildTarget)
	{
		string buildScript = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../BuildSDK.py"));
		if (!File.Exists(buildScript))
		{
			throw new BuildException($"[NGVulkanBackend] BuildSDK.py not found at: {buildScript}");
		}

		Console.WriteLine($"[NGVulkanBackend] Prebuilt binary missing — running: python BuildSDK.py -b {buildTarget}");

		var psi = new ProcessStartInfo
		{
			FileName = "python",
			Arguments = $"\"{buildScript}\" -b {buildTarget}",
			WorkingDirectory = Path.GetDirectoryName(buildScript),
			UseShellExecute = false,
			RedirectStandardOutput = false,  // let stdout stream live to the UBT console
			RedirectStandardError = true,
		};

		using (var proc = Process.Start(psi))
		{
			// Read stderr asynchronously so it doesn't deadlock while stdout streams live.
			var stderrTask = proc.StandardError.ReadToEndAsync();
			proc.WaitForExit();
			string stderr = stderrTask.Result;

			if (proc.ExitCode != 0)
			{
				throw new BuildException($"[NGVulkanBackend] BuildSDK.py -b {buildTarget} failed (exit {proc.ExitCode}):\n{stderr}");
			}
		}

		Console.WriteLine($"[NGVulkanBackend] BuildSDK.py -b {buildTarget} completed successfully.");
	}

	// Stage the prebuilt NG-feature Vulkan layer artifact (and its JSON
	// manifest, when applicable) next to the game binary. The layer itself
	// is produced by NG-SDK's CMake build (target NGLayer, nglayer/) and
	// shipped via Binaries/ThirdParty/PreBuiltNGSDK/, so this module just
	// references the prebuilt path.
	//
	//   subDir              optional sub-folder under PreBuiltNGSDK/ (used
	//                        for Linux per-arch variants).
	//   layerFileName       e.g. "libVkLayer_arm_NG.so" or the
	//                        Windows .dll.
	//   includeJsonManifest also stage VkLayer_arm_NG.json
	//                        (required on Win/Linux loader; not needed on
	//                        Android — APL.xml drops the .so directly into
	//                        the APK's libs/ABI/ dir).
	//   requireExists       if true, BuildException when the artifact is
	//                        missing; if false, silently skip (used for the
	//                        optional Linux variants).
	private void AddPrebuiltNGLayer(string prebuiltPath, string subDir,
	                                string layerFileName, bool includeJsonManifest,
	                                bool requireExists = true)
	{
		string baseDir = subDir == null
			? prebuiltPath
			: Path.Combine(prebuiltPath, subDir);
		string layerSrc = Path.Combine(baseDir, layerFileName);

		if (!File.Exists(layerSrc))
		{
			string msg = $"[NGVulkanBackend] NGLayer artifact not found: {layerSrc}";
			if (requireExists)
				throw new BuildException(msg + "\nBuild NG-SDK (which produces nglayer/ by default) and re-run BuildSDK to populate PreBuiltNGSDK/.");
			Console.WriteLine(msg + " — skipping (optional for this build context).");
			return;
		}

		RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", layerFileName), layerSrc);

		if (includeJsonManifest)
		{
			string jsonSrc = Path.Combine(baseDir, "VkLayer_arm_NG.json");
			if (File.Exists(jsonSrc))
			{
				RuntimeDependencies.Add(
					Path.Combine("$(TargetOutputDir)", "VkLayer_arm_NG.json"),
					jsonSrc);
			}
		}
	}
}

