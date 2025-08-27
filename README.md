<!-- 
// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Super Sampling Unreal® Engine Plugin

## Introduction

Neural Super Sampling is a mobile-optimized temporal upscaling technique that uses machine learning to improve graphical fidelity, while reducing computational cost.

This Unreal® Engine plugin provides an implementation of the Neural Super Sampling technique to be used with Unreal® projects. It implements the `UE::Renderer::Private::ITemporalUpscaler` interface provided by the engine so it can be integrated into your game.

**This early release plugin is intended for image quality evaluation and algorithm inspection in a desktop environment. Runtime performance is not representative of what can be expected on mobile platforms. For a more optimized implementation of the shaders, refer to the following .comp files: https://huggingface.co/Arm/neural-super-sampling/tree/a90431d/scenario**

This plugin uses Unreal® Neural Network Engine to perform inference of the machine learning model.

## System requirements

- Windows 11
- Unreal® Engine 5.5
- Visual Studio 2022, with the "Desktop Development with C++" and ".NET desktop build tools" packs enabled
- An NNE runtime which can run the neural network model which this plugin uses.
  - Currently this is only `NNERuntimeRDGMLExtensionsForVulkan`, which is available as a separate plugin. Please refer to that plugin's documentation.

## Setup

1. **If you downloaded this plugin from the *GitHub release package*, then skip this step.**

    Make sure there is a `.vgf` file in the `Content` folder of this plugin. This is included as part of the *GitHub release package*, but is not part of the Git repository as it is an external dependency. It can be downloaded separately from https://huggingface.co/Arm/neural-super-sampling/resolve/a90431d/nss_v0.1.1_int8.vgf. The file must be downloaded to `Content/nss_v0_1_1_int8.vgf`. (Note the change from periods to underscores)

2. Copy this folder into the `Plugins/` folder in either your Unreal® `Engine` folder or your project folder.

3. Enable the plugin (e.g. in your `.uproject` file or using the Plugins window).

4. Build the engine for your project.

5. Play your level in a New Editor Window.

6. NSS should now be running. This can be confirmed by setting `ShowFlag.VisualizeTemporalUpscaler 1` and checking which upscaler is being used.

## Troubleshooting

If `ShowFlag.VisualizeTemporalUpscaler 1` does not show that NSS is being used, make sure the following console variables are set:

```
r.AntiAliasingMethod 2
r.TemporalAA.Upscaler 1
r.TemporalAA.Upscaling 1
r.NSS.Enable 1
```

## Trademarks and Copyrights

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.


