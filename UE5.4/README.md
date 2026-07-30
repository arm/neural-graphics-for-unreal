<!--
// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Graphics Plugin for Unreal® Engine 5.4

## Introduction

This plugin uses the [Neural Graphics SDK for Game Engines](https://github.com/arm/neural-graphics-sdk-for-game-engines), which provides an API compatible with AMD FidelityFX™, to enable machine learning based super resolution and frame interpolation technologies in Unreal® Engine.
The Neural Graphics SDK for Game Engines is included as a submodule within the UE5.4 plugin. After cloning this repository, run:
```bash
git submodule sync
git submodule update --init --recursive
```

The source code of the Neural Graphics SDK for Game Engines can be found under:
```text
Source\NG-SDK\
```

Some large binary assets are stored using Git LFS. Ensure that Git LFS is installed and initialized:
```bash
git lfs install
git lfs pull
git submodule foreach "git lfs pull"
```

## System Requirements

- Windows 11
- Android™ 16 or later
- Unreal® Engine 5.4
- Visual Studio 2022, with the "Desktop Development with C++" and ".NET desktop build tools" packs enabled
- Vulkan SDK version 1.4.321.0 or later for Windows.

## Installation on Windows

This installation guide assumes that you have an Unreal® C++ project you intend to use this plugin with &ndash; if not, create one as a first step. We also assume that you have downloaded and &ndash; if needed &ndash; unzipped the plugin code. If you are reading this document online, download the latest plugin source code to your machine, then:

1. Create a `Plugins` folder in either your Unreal® `Engine` folder or your project root folder (if one doesn't exist already).

2. Copy this plugin folder (the parent folder of this `README.md` file) into the `Plugins\` folder in either your Unreal® `Engine` folder or your project folder.

3. Enable the plugin (e.g. in your `.uproject` file or using the Plugins window).

4. On Windows, the `VK_ARM_tensors` and `VK_ARM_data_graph` Vulkan extensions required by the Neural Graphics Plugin are typically not available through native GPU drivers. To run the plugin, install and enable the Vulkan emulation layers. Please refer to the  [Setting up the emulation layers | Arm Learning Paths](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/2-emulation-layer/).

5. Re-generate Visual Studio® project files and recompile your project.

6. Enable one or both of the supported Neural Graphics features in **Project Settings > Plugins > Neural Graphics 1.1.0**:
   -  NSS (Neural Super Sampling)
   -  NFRU (Neural Frame Rate Upscaling)

7. Verify that the enabled feature is active:
   - NSS:
     Run `ShowFlag.VisualizeTemporalUpscaler 1` in the Unreal® Editor console and confirm that `ThirdParty TemporalUpscaler` reports NSS.
   - NFRU:
     Run `r.NFRU.ShowDebugView 1` and confirm that the debug view is displayed.

## Building for Android

For general instructions about building and deploying on Android, please refer to the following official Unreal® Engine guides:
* [Setting Up Android SDK and NDK](https://dev.epicgames.com/documentation/en-us/unreal-engine/set-up-android-sdk-ndk-and-android-studio-using-turnkey-for-unreal-engine?application_version=5.4)
* [Android Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-unreal-engine-projects-for-android-development)
* [Android Vulkan Mobile Renderer](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-android-vulkan-mobile-renderer-in-unreal-engine?application_version=5.4)

## Mobile Renderer

- To use **NSS** with TAA on Mobile Renderer (ES31 shader platform), apply the following UE5.4 patch:
  [UE5.4 Mobile TAA Patch](https://github.com/EpicGames/UnrealEngine/commit/35ea9179968049dd607668eb0c272f3b7370b2a4)

  After completing the standard NSS configuration, also enable the following console variables for Mobile TAA:
  ```text
  r.Mobile.AntiAliasing 2
  r.Mobile.SupportsGen4TAA 1
  ```

  These variables can be set at runtime through a console or configured persistently in your project's Config/DefaultEngine.ini file:
  ```
  [/Script/Engine.RendererSettings]
  r.Mobile.AntiAliasing=2
  r.Mobile.SupportsGen4TAA=True
  ```

  If these settings are not enabled, NSS will not be available on the Mobile Renderer.

- To use **NFRU** on Mobile Renderer (ES31 shader platform), apply the following UE5.4 patch:

  [NFRU Mobile Renderer Patch](https://github.com/EpicGames/UnrealEngine/commit/7d765815f22bf)

## Additional Configuration

The following console variables can be used to control NSS and NFRU shader behavior:

1. `r.NSS.UseFragmentShader` selects the shader path for NSS pre-processing and post-processing passes:
   ```text
   0: compute shader path
   1 (default): fragment shader path
   ```

2. `r.NFRU.UseFragmentShader` selects the shader path for NFRU pre-processing and post-processing passes:
   ```text
   0: compute shader path
   1 (default): fragment shader path
   ```

3. `r.NSS.ShaderQualityMode` selects the quality mode for NSS shader passes:
   ```text
   0: highest quality, highest GPU cost
   1 (default): balanced quality and performance
   2: optimized for performance, with reduced quality
   ```

These variables can be configured persistently in an `.ini` file or changed at runtime through a console.

## Troubleshooting

1. If you have modified the SDK source code, you need to rebuild and update the SDK binaries under `Binaries\ThirdParty\PreBuiltNGSDK\`.

   From the plugin root directory:
   Windows:
   ```shell
   python BuildSDK.py -b vk_windows_x64
   ```
   Android:
   ```shell
   python BuildSDK.py -b vk_android_arm
   ```

2. If NSS is enabled but `ShowFlag.VisualizeTemporalUpscaler 1` does not report NSS as the active temporal upscaler, make sure the following settings are enabled:
   ```shell
   r.AntiAliasingMethod 2
   r.TemporalAA.Upscaler 1
   r.TemporalAA.Upsampling 1
   r.NSS.Enable 1
   ```
   Setting `r.AntiAliasingMethod` to 4 is also supported.

3. The Unreal starter content is not optimized for mobile renderer preview. As a result, visual quality may appear degraded. This is not caused by NSS/NFRU or the Neural Graphics Plugin. If required, enabling FP32 for material expressions can improve preview quality, but this comes with a performance tradeoff (see [Mali best practices]( https://developer.arm.com/documentation/101897/0304/Shader-code/Minimize-precision?lang=en)).

4. The CVar `r.NFRU.OnlyInterpolatedFrames` is an experimental feature and intended for development and debugging purposes only. It displays only interpolated frames when NFRU is enabled. By default it is disabled.

5. NFRU supports the following back buffer formats: PF_R8G8B8A8, PF_B8G8R8A8, PF_FloatR11G11B10, and PF_R8. NFRU is automatically disabled when an unsupported back buffer format is detected.

   To ensure compatibility, we recommend using the following back buffer setting:
   ```ini
   [/Script/Engine.RendererSettings]
   r.DefaultBackBufferPixelFormat=0
   ```

## Known Issues

- Visual artifacts may be observed when NSS is enabled and `r.ScreenPercentage` is set below 35. This limitation is known and is being investigated. To avoid these artifacts, `r.ScreenPercentage` should be kept within the range [35, 100].

- In very rare cases, certain upscale ratios can trigger float32 precision issues in the dynamic offset LUT generation path, which may manifest as visible black line artifacts in the upscaled output. As a workaround, users are advised to adjust the upscale ratio to avoid affected configurations until a permanent fix is available.

- NSS operates on linear HDR color data. For an optimal balance between performance and image quality, NSS defaults to the widely adopted **R11G11B10** format for HDR resources.

  On some hardware platforms, the R11G11B10 format may introduce dark speckle artifacts due to precision limitations when writing HDR color data. An optional workaround is documented in the Neural Graphics SDK for Game Engines README. The workaround may introduce additional shader overhead and is therefore not enabled by default.

- In Unreal® Engine 5.4, enabling `Stat FrameGen` while running NFRU may cause UI flickering. To avoid this issue, set the console variable `r.NFRU.OnlyInterpolatedFrames` to `1`.

## References

For more information, refer to the learning paths for [NSS](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/) and [NFRU](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nfru-unreal/).

## Trademarks and Copyrights

AMD is a trademark of Advanced Micro Devices, Inc.

AMD FidelityFX™ is a trademark of Advanced Micro Devices, Inc.

Android is a trademark of Google LLC.

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio and Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
