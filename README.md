<!--
// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Graphics Plugin for Unreal® Engine 5

## Introduction

This plugin uses the [Neural Graphics SDK for Game Engines](https://github.com/arm/neural-graphics-sdk-for-game-engines), which provides an API compatible with AMD FidelityFX™, to enable machine learning based super resolution and frame interpolation technologies in Unreal® Engine.
The SDK is maintained as a Git submodule under `Source\NG-SDK\`.

The plugin currently supports only the Vulkan rendering backend on Windows and Android.

## Getting the source and dependencies

### Git checkout

Install Git LFS, then clone the public branch with its submodules and download the binary assets:

```bash
git lfs install
git clone --branch ue5 --recurse-submodules https://github.com/arm/neural-graphics-for-unreal.git
cd neural-graphics-for-unreal
git lfs pull
git submodule foreach --recursive "git lfs pull"
```

### Source archives

Repository-generated source ZIP archives do not include Git submodule contents. Use a Git checkout with initialized submodules to obtain the SDK revision matching the plugin; running `git submodule update` inside an extracted ZIP does not initialize it as a Git checkout.

## System Requirements and Compatibility

The following summary covers Unreal Engine plugin requirements and SDK-recommended neural model and external-tool versions. See [SDK documentation](#sdk-documentation) for complete SDK compatibility and Vulkan runtime requirements.

### Platforms

| Platform | Requirement | Notes |
|----------|-------------|-------------|
| Windows | Windows 11 | Requires [Arm ML Emulation Layer for Vulkan](https://github.com/arm/ai-ml-emulation-layer-for-vulkan). |
| Android™ | Android 17 or later | Requires a compatible Mali GPU driver with native support for the required Vulkan tensor and data graph extensions. NSS and NFRU are automatically disabled if the required support is unavailable. |

### Neural models

Plugin v1.1.1 is compatible with SDK v1.1.1 and uses the following neural model versions:

| Feature | Model version | Source |
|---------|---------------|--------|
| NSS (Neural Super Sampling) | `1.0.1` | https://huggingface.co/Arm/neural-super-sampling |
| NFRU (Neural Frame Rate Upscaling) | `2.0.0` | https://huggingface.co/Arm/neural-frame-rate-upscaling |

These models are embedded in the prebuilt SDK binaries; no separate model download is required.

### External tools

| Tool | Version / requirement | Purpose and notes |
|------|-----------------------|-------------------|
| [Unreal® Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/installing-unreal-engine) | `5.4`, `5.6` | Engine versions supported by this plugin. UE 5.4 mobile rendering requires additional patches. |
| [Visual Studio®](https://visualstudio.microsoft.com/vs/older-downloads/) | `2022` | Windows compilation; install the "Desktop Development with C++" and ".NET desktop build tools" workloads. |
| [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) | `1.4.350.0` or later recommended | Provides Vulkan headers and shader build tools. This is not the Vulkan runtime API version requirement. |
| [Python](https://www.python.org/downloads/) | `3.7` or later | Required by the SDK build scripts and `DownloadEmulationLayer.bat`. |
| [Arm ML Emulation Layer for Vulkan](https://github.com/arm/ai-ml-emulation-layer-for-vulkan) | `v0.11.0` | SDK-recommended version for devices without native Arm ML extension support. |
| RenderDoc for Arm GPUs | `2026.5` | Optional graphics debugging tool, included in [Arm Performance Studio](https://developer.arm.com/tools-and-software/arm-performance-studio#Downloads). Not required to build or run the plugin. |

### Rebuilding the SDK (optional)

The plugin includes prebuilt SDK binaries, so rebuilding the SDK is normally unnecessary. If you need to rebuild it, see the [SDK Compatibility Matrix](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/compatibility_matrix.md#build-tool-versions) for prerequisites.

To view build options, run from the plugin root:

```shell
python BuildSDK.py --help
```

## Installation on Windows

Start with an Unreal® Engine C++ project and obtain the plugin source and dependencies as described in [Getting the source and dependencies](#getting-the-source-and-dependencies).

1. Create a `Plugins` folder in your Unreal® Engine folder or project root if needed, then copy the plugin folder into it.

2. Enable the plugin in your `.uproject` file or through the Unreal Editor's **Plugins** window.

3. Prepare and compile your project:

   For mobile rendering, first complete the setup in [Mobile Renderer](#mobile-renderer), including the engine patches and rebuild required for UE 5.4.

   Then re-generate Visual Studio® project files and recompile your project.

4. Before running the Editor or game with Vulkan on Windows, install and enable the emulation layers for `VK_ARM_tensors` and `VK_ARM_data_graph`.

   From the plugin root, double-click `DownloadEmulationLayer.bat` or run:

   ```bat
   .\DownloadEmulationLayer.bat
   ```

   The script installs the layers to `Binaries\ThirdParty\VulkanMLEmulation\Win64\`.

   **Downloading does not enable the layers.** Follow [Setting up the emulation layers](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/2-emulation-layer/) to enable them, using the downloaded location above.

   If Vulkan validation is enabled, see [Troubleshooting](#troubleshooting), item 4.

5. In the Unreal Editor, set **Project Settings > Platforms > Windows > Default RHI** to **Vulkan**, then restart the Editor.

6. Enable one or both of the supported Neural Graphics features in **Project Settings > Plugins > Neural Graphics 1.1.1**:
   -  NSS (Neural Super Sampling)
   -  NFRU (Neural Frame Rate Upscaling)

7. Verify that the enabled feature is active:

   - NSS:

     Run `ShowFlag.VisualizeTemporalUpscaler 1` in the Unreal® Editor console and confirm that `ThirdParty TemporalUpscaler` reports NSS.

     Alternatively, run `r.NSS.Debug 1` and confirm that the NSS diagnostic views appear. Set `r.NSS.Debug 0` afterwards.

   - NFRU:

     Run `r.NFRU.ShowDebugView 1` and confirm that the debug view is displayed.

## Building for Android

For general instructions about building and deploying on Android, please refer to the following official Unreal® Engine guides:
* [Setting Up Android SDK and NDK](https://dev.epicgames.com/documentation/en-us/unreal-engine/set-up-android-sdk-ndk-and-android-studio-using-turnkey-for-unreal-engine?application_version=5.4)
* [Android Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-unreal-engine-projects-for-android-development)
* [Android Vulkan Mobile Renderer](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-android-vulkan-mobile-renderer-in-unreal-engine?application_version=5.4)

## Mobile Renderer

- To use **NSS** on Mobile Renderer (ES31 shader platform), apply the following patch for UE 5.4 only:
  [UE5.4 Mobile TAA Patch](https://github.com/EpicGames/UnrealEngine/commit/35ea9179968049dd607668eb0c272f3b7370b2a4)

  For all supported engine versions, configure the following Mobile TAA settings in Project Settings or your project's `Config/DefaultEngine.ini` file:

  ```
  [/Script/Engine.RendererSettings]
  r.Mobile.AntiAliasing=2
  r.Mobile.SupportsGen4TAA=True
  ```

  If these settings are not enabled, NSS will not be available on the Mobile Renderer.

- To use **NFRU** on Mobile Renderer (ES31 shader platform), apply the following patch for UE 5.4 only:
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

1. If NSS is enabled but `ShowFlag.VisualizeTemporalUpscaler 1` does not report NSS as the active temporal upscaler, make sure the following settings are enabled:
   ```shell
   r.AntiAliasingMethod 2
   r.TemporalAA.Upscaler 1
   r.TemporalAA.Upsampling 1
   r.NSS.Enable 1
   ```
   Setting `r.AntiAliasingMethod` to 4 is also supported.

2. The Unreal starter content is not optimized for mobile renderer preview. As a result, visual quality may appear degraded. This is not caused by NSS/NFRU or the Neural Graphics Plugin. If required, enabling FP32 for material expressions can improve preview quality, but this comes with a performance tradeoff (see [Mali best practices]( https://developer.arm.com/documentation/101897/0304/Shader-code/Minimize-precision?lang=en)).

3. NFRU supports the following back buffer formats: PF_R8G8B8A8, PF_B8G8R8A8, PF_FloatR11G11B10, and PF_R8. NFRU is automatically disabled when an unsupported back buffer format is detected.

   To ensure compatibility, we recommend using the following back buffer setting:
   ```ini
   [/Script/Engine.RendererSettings]
   r.DefaultBackBufferPixelFormat=0
   ```

4. On Windows, with both ML emulation layers installed and enabled, use the following layer order when enabling `VK_LAYER_KHRONOS_validation`, from the application toward the GPU driver:

   ```text
   VK_LAYER_ML_Graph_Emulation
   VK_LAYER_ML_Tensor_Emulation
   VK_LAYER_KHRONOS_validation
   ```

   This allows the emulation layers to translate Arm tensor and data graph calls before validation. Placing the validation layer before either emulation layer may cause validation errors or crashes. Alternatively, disable the validation layer if it is not needed.

## Known Issues

- Visual artifacts may be observed when NSS is enabled and `r.ScreenPercentage` is set below 35. This limitation is known and is being investigated. To avoid these artifacts, `r.ScreenPercentage` should be kept within the range [35, 100].

- In very rare cases, certain upscale ratios can trigger float32 precision issues in the dynamic offset LUT generation path, which may manifest as visible black line artifacts in the upscaled output. As a workaround, users are advised to adjust the upscale ratio to avoid affected configurations until a permanent fix is available.

- On some devices, writing HDR color in NSS's default **R11G11B10** format may produce dark speckle artifacts due to precision limitations. See the [NSS guide](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/NSS/NSS.md#providing-color-and-history) for an optional workaround, which may add shader overhead.

- In Unreal® Engine 5.4, enabling `Stat FrameGen` while running NFRU may cause UI flickering. For debugging, set the console variable `r.NFRU.OnlyInterpolatedFrames` to `1` as a workaround. This displays only interpolated frames and is not intended for normal use.

## References

### Learning paths

For step-by-step Unreal Engine integration tutorials, see the learning paths for [NSS](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/) and [NFRU](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nfru-unreal/).

### SDK documentation

For SDK usage and API details, see the links below.

- [SDK README](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/README.md): SDK overview and build instructions.
- [Compatibility Matrix](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/compatibility_matrix.md): model and tool versions, Vulkan runtime requirements, device features, and backend constraints.
- [User Guide](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/user_guide.md): integration steps and ML emulation-layer configuration.
- [NSS guide](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/NSS/NSS.md) and [NFRU guide](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/main/docs/NFRU/NFRU.md): feature-specific integration and resource requirements.

## Trademarks and Copyrights

AMD is a trademark of Advanced Micro Devices, Inc.

AMD FidelityFX™ is a trademark of Advanced Micro Devices, Inc.

Android is a trademark of Google LLC.

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio and Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
