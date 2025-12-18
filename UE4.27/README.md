<!--
// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# NSS Plugin for UE4.27

## Introduction

This plugin calls Neural Graphics SDK for Game Engines (https://github.com/arm/neural-graphics-sdk-for-game-engines) whose API is compliant with AMD FidelityFX API to perform inference of the machine learning model and super sampling processing. The Neural Graphics SDK for Game Engines is a submodule within the UE4.27 plugin. You will need to run `git submodule update --init` after cloning this repository. Then in folder `Source\NG-SDK\`, you can find the source code of the Neural Graphics SDK for Game Engines. Some big binary files are stored in Github LFS, you will need to run `git lfs install` to make sure you have installed git-lfs and initialized it. Then you will need to run `git lfs pull` to pull all LFS files.

## System Requirements

- Windows 11
- Unreal® Engine 4.27
- Visual Studio 2019, with the "Desktop Development with C++" and ".NET desktop build tools" packs enabled
- Vulkan Software Development Kit for Windows platform version 1.4.321.0 or newer.

## Installation

This installation guide assumes that you have an Unreal® C++ project you intend to use this plugin with &ndash; if not, create one as a first step. We also assume that you have downloaded and &ndash; if needed &ndash; unzipped the plugin code. If you are reading this document online, download the latest plugin source code to your machine, then:

1. Create a `Plugins` folder in either your Unreal® `Engine` folder or your project root folder (if one doesn't exist already).

2. Copy this plugin folder (the parent folder of this `README.md` file) into the `Plugins\` folder in either your Unreal® `Engine` folder or your project folder.

3. Enable the plugin (e.g. in your `.uproject` file or using the Plugins window).

4. Re-generate Visual Studio® project files and recompile your project.

5. Start your project in Unreal® Editor and check that the NSS plugin is enabled. If not, enable it. This can be confirmed by setting `r.NSS.Debug 1` and checking the output debug view.

## Troubleshooting

If `r.NSS.Debug 1` does not show that NSS is being used, make sure the following console variables are set:

```
r.TemporalAA.Upscaler 1
r.TemporalAA.Upsampling 1
r.NSS.Enable 1
r.ScreenPercentage 50
```

If you changed the source code of NG-SDK, you need to update the prebuilt_binaries of NG-SDK, just double click the **BuildSDK.bat**.

If you want to enable Vulkan® ES3.1 or mobile rendering for your project, please make sure the following console variable is set in DefaultEngine.ini under your project:
```
[/Script/Engine.RendererSettings]
r.Mobile.SupportsGen4TAA=True
```
Otherwise, NSS will not be enabled for Vulkan® ES3.1 or mobile rendering mode.

## Known Issues

If NSS is enabled and `r.ScreenPercentage` is set as lower than 35, such as below console variables:
```
r.TemporalAA.Upscaler 1
r.TemporalAA.Upsampling 1
r.NSS.Enable 1
r.ScreenPercentage 25
```
In this case, the UE editor may show some artifacts in rendered frames. This is a known issues, we may fix it in future release. So currently we don't recommend setting `r.ScreenPercentage` as lower than 35. And also `r.ScreenPercentage` should not be set as higher than 100.

## References

For more information, refer to the [learning paths](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/).

## Trademarks and Copyrights

AMD is a trademark of Advanced Micro Devices, Inc.

AMD FidelityFX™ is a trademark of Advanced Micro Devices, Inc.

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
