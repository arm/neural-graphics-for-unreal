<!--
// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# NSS Plugin for UE5.4

## Introduction

This plugin calls Neural Graphics SDK for Game Engines (https://github.com/arm/neural-graphics-sdk-for-game-engines) whose API is compliant with AMD FidelityFX API to perform inference of the machine learning model and super sampling processing. The Neural Graphics SDK for Game Engines is a submodule within the UE5.4 plugin. You will need to run `git submodule update --init` after cloning this repository. Then in folder `Source\NG-SDK\`, you can find the source code of the Neural Graphics SDK for Game Engines. Some big binary files are stored in Github LFS, you will need to run `git lfs install` to make sure you have installed git-lfs and initialized it. Then you will need to run `git lfs pull` to pull all LFS files.

## System Requirements

- Windows 11
- Unreal® Engine 5.4
- Visual Studio 2022, with the "Desktop Development with C++" and ".NET desktop build tools" packs enabled
- Vulkan Software Development Kit for Windows platform version 1.4.313.0 or newer.

## Installation

This installation guide assumes that you have an Unreal® C++ project you intend to use this plugin with &ndash; if not, create one as a first step. We also assume that you have downloaded and &ndash; if needed &ndash; unzipped the plugin code. If you are reading this document online, download the latest plugin source code to your machine, then:

1. Create a `Plugins` folder in either your Unreal® `Engine` folder or your project root folder (if one doesn't exist already).

2. Copy this plugin folder (the parent folder of this `README.md` file) into the `Plugins\` folder in either your Unreal® `Engine` folder or your project folder.

3. Enable the plugin (e.g. in your `.uproject` file or using the Plugins window).

4. Re-generate Visual Studio® project files and recompile your project.

5. Start your project in Unreal® Editor and check that the NSS plugin is enabled. If not, enable it. This can be confirmed by setting ShowFlag.VisualizeTemporalUpscaler 1 and checking which upscaler is being used.

## Troubleshooting

If `ShowFlag.VisualizeTemporalUpscaler 1` does not show that NSS is being used, make sure the following console variables are set:

```
r.AntiAliasingMethod 2      # With **r.AntiAliasingMethod 4** we can also use NSS.
r.TemporalAA.Upscaler 1
r.TemporalAA.Upsampling 1
r.NSS.Enable 1
r.ScreenPercentage 50
```

If you want to use TAA for Mobile Render(ES31 shader platform), you need to apply this patch for UE5.4.
```
https://github.com/EpicGames/UnrealEngine/commit/35ea9179968049dd607668eb0c272f3b7370b2a4
```
And you need to set an additional console variable:
```
r.Mobile.AntiAliasing 2  # Set TAA on Mobile Render
```

If you changed the source code of SDK, you need to update the prebuilt_binaries of SDK in folder `Source\NG-SDK\prebuilt_binaries\`, just double click the **BuildSDK.bat**.

The Unreal starter content is not optimized for mobile renderer preview. As a result, visual quality may appear degraded. This is not related to the NSS model or the NSS Plugins. If required, enabling FP32 for material expressions can improve preview quality, but this comes with a performance tradeoff (see Mali best practices - https://developer.arm.com/documentation/101897/0304/Shader-code/Minimize-precision?lang=en).

## References

For more information, refer to the [learning paths](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/).

## Trademarks and Copyrights

AMD is a trademark of Advanced Micro Devices, Inc.

AMD FidelityFX™ is a trademark of Advanced Micro Devices, Inc.

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
