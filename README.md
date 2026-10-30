<!--
// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Graphics Plugin for Unreal Engine

## Introduction

The Arm Neural Graphics Plugin provides neural upscaling and frame generation
for Unreal Engine through the [Neural Graphics SDK for Game Engines](https://github.com/arm/neural-graphics-sdk-for-game-engines)
and its Vulkan backend. In UE5, NSS and NFRU can be enabled independently or
together.

### Neural Super Sampling (NSS)

NSS uses a neural network and temporal information to reconstruct
display-resolution images from lower-resolution renders. Integrated into
Unreal Engine's temporal upscaling pipeline, it helps balance image quality and
rendering cost.

### Neural Frame Rate Upscaling (NFRU)

NFRU uses neural frame interpolation to generate an intermediate frame between
two rendered frames, improving perceived frame rate and motion smoothness.
The plugin integrates frame generation into Unreal Engine's presentation
pipeline, allowing generated frames to be displayed alongside rendered frames.

## Choose a branch

This branch contains documentation only. Plugin source is maintained on
separate branches:

| Branch | Contents | Supported Unreal Engine versions |
| --- | --- | --- |
| [`main`](https://github.com/arm/neural-graphics-for-unreal/tree/main) | Documentation and branch catalog | Not applicable |
| [`ue5`](https://github.com/arm/neural-graphics-for-unreal/tree/ue5) | Neural Graphics Plugin with NSS and NFRU | 5.4 and 5.6 |
| [`ue4.27`](https://github.com/arm/neural-graphics-for-unreal/tree/ue4.27) | NSS plugin | 4.27 |
| [`DemoAssets_EnchantedCastle`](https://github.com/arm/neural-graphics-for-unreal/tree/DemoAssets_EnchantedCastle) | Enchanted Castle demo project | 5.4 or later |

Each source branch places its plugin at the repository root.
Read that branch's README for installation instructions and requirements.
The UE4.27 plugin remains on the older SDK integration; it does not provide
the UE5 plugin's NFRU functionality.

## Getting started

Install Git and [Git LFS](https://git-lfs.com/), then clone the branch matching
your Unreal Engine version. For example, for UE5:

```shell
git lfs install
git clone --branch ue5 --recurse-submodules https://github.com/arm/neural-graphics-for-unreal.git
```

Run `git lfs pull` in the cloned repository and its initialized submodules
when required by the selected branch's README.

### Source archives

Plugin source ZIPs do not include Git submodules. Clone the appropriate plugin
branch with `--recurse-submodules` and follow its README for setup.

## Documentation

- [UE5 setup and usage](https://github.com/arm/neural-graphics-for-unreal/blob/ue5/README.md)
- [UE4.27 setup and usage](https://github.com/arm/neural-graphics-for-unreal/blob/ue4.27/README.md)
- [NSS learning path](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/)
- [NFRU learning path](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nfru-unreal/)

## License

Plugin source is licensed under the [MIT License](LICENSE), except where a
file carries a different notice. Preserve the applicable third-party notices.
The demo branch includes its own licenses and attribution; in particular,
LTween is licensed under CC BY 4.0.

## Trademarks

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
