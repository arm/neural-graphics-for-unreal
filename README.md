<!--
// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Super Sampling Plugin for Unreal® Engine 4.27

## Introduction

This plugin integrates Arm Neural Super Sampling (NSS) into Unreal Engine's temporal upscaling pipeline. NSS uses a neural network and information from previous frames to reconstruct higher-resolution images from lower-resolution renders.

The plugin uses the Vulkan backend of the [Neural Graphics SDK for Game Engines v1.0.0](https://github.com/arm/neural-graphics-sdk-for-game-engines/tree/v1.0.0), which provides an API compatible with AMD FidelityFX™. The SDK is maintained as a Git submodule under `Source\NG-SDK\`.

The plugin currently supports only the Vulkan rendering backend on Windows.

## Getting the source and dependencies

### Git checkout

Install Git and [Git LFS](https://git-lfs.com/). For a new checkout, clone the UE4.27 branch:

```shell
git clone --branch ue4.27 https://github.com/arm/neural-graphics-for-unreal.git
cd neural-graphics-for-unreal
```

For both new and existing checkouts, run the following from the plugin root to initialize the dependencies and download the binary assets before building:

```shell
git lfs install
git submodule sync --recursive
git submodule update --init --recursive
git lfs pull
git submodule foreach --recursive "git lfs pull"
```

Use the SDK revision pinned by this branch. Do not update the submodule to the latest SDK independently.

### Source archives

Repository-generated source ZIP archives do not include Git submodule contents. Use a Git checkout with initialized submodules to obtain the matching SDK and binary assets; running `git submodule update` inside an extracted ZIP does not initialize it as a Git checkout.

## System Requirements and Compatibility

### Windows tools

| Component | Requirement |
|-----------|-------------|
| Windows | Windows 11, x64. |
| Unreal® Engine | `4.27`. |
| Visual Studio® | `2019`, with the "Desktop Development with C++" and ".NET desktop build tools" workloads. |
| Vulkan SDK | `1.4.321.0` or newer. This is a development-tool requirement, not the Vulkan runtime API version. |
| CMake | `3.21` to `3.31`, as specified by the pinned SDK; required when rebuilding SDK binaries. |

### SDK and neural model

| Component | Version | Source |
|-----------|---------|--------|
| Neural Graphics SDK for Game Engines | `1.0.0` | [SDK v1.0.0](https://github.com/arm/neural-graphics-sdk-for-game-engines/tree/v1.0.0) |
| NSS model | `0.1.1` (INT8) | [Arm neural-super-sampling v0.1](https://huggingface.co/Arm/neural-super-sampling/tree/main/v0_1) |

The SDK embeds the NSS model in its runtime binary; no separate model download is required.

## Installation on Windows

Start with an Unreal Engine 4.27 C++ project and a complete Git checkout as described in [Getting the source and dependencies](#getting-the-source-and-dependencies).

1. Create a `Plugins` folder in your project root or Unreal Engine folder if it does not already exist.

2. Copy the plugin root, containing `NSS.uplugin` and this README, into a subfolder such as `Plugins\NSS\`.

3. Enable **Neural Super Sampling (NSS)** in the Plugins window or your `.uproject` file.

4. Re-generate Visual Studio project files and rebuild your project.

5. Configure the project to use Vulkan and Temporal Anti-Aliasing (TAA). NSS is not available with the DirectX RHIs.

6. Configure the bundled Graph and Tensor layers with Vulkan Configurator by following the [Arm Learning Path configuration instructions](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/2-emulation-layer/#configure-vulkan-layers). Use the absolute path to the plugin's `Binaries\ThirdParty\VulkanMLEmulation\Win64\` directory as the Vulkan layers location.

7. In Unreal® Editor, enable NSS in **Project Settings > Plugins > Neural Super Sampling 1.0**, then run `r.NSS.Debug 1` to confirm that the NSS debug view appears.

## Troubleshooting

1. If `r.NSS.Debug 1` does not show that NSS is being used, make sure the following console variables are set:

   ``` plaintext
   r.TemporalAA.Upscaler 1
   r.TemporalAA.Upsampling 1
   r.NSS.Enable 1
   ```

   For a regular editor viewport, also set:

   ```plaintext
   r.NSS.EnableInEditorViewport 1
   ```

2. If you want to enable Vulkan® ES3.1 or mobile rendering for your project, please make sure the following console variable is set in DefaultEngine.ini under your project:

   ```plaintext
   [/Script/Engine.RendererSettings]
   r.Mobile.SupportsGen4TAA=True
   ```

   Otherwise, NSS will not be enabled for Vulkan® ES3.1 or mobile rendering mode.

3. If you changed the source code of SDK, you need to update the prebuilt_binaries of SDK in folder `Source\NG-SDK\prebuilt_binaries\`, just double click the **BuildSDK.bat**.

4. The Unreal starter content is not optimized for mobile renderer preview. As a result, visual quality may appear degraded. This is not related to the NSS model or the NSS Plugins. If required, enabling FP32 for material expressions can improve preview quality, but this comes with a performance tradeoff (see Mali best practices - https://developer.arm.com/documentation/101897/0304/Shader-code/Minimize-precision?lang=en).

## Known Issues

- Visual artifacts may appear when `r.ScreenPercentage` is below `35`. Use a value of at least `35` and below `100`.

## References

- [SDK v1.0.0 README](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/v1.0.0/README.md): SDK requirements and build instructions.
- [SDK v1.0.0 NSS guide](https://github.com/arm/neural-graphics-sdk-for-game-engines/blob/v1.0.0/docs/NSS/NSS.md): NSS integration and resource requirements.
- [NSS learning path](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/): general background and Vulkan Configurator setup; follow this README for UE4.27-specific plugin installation and NSS settings.

## Trademarks and Copyrights

AMD is a trademark of Advanced Micro Devices, Inc.

AMD FidelityFX™ is a trademark of Advanced Micro Devices, Inc.

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
