<!--
// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Super Sampling Unreal® Engine Plugin

Neural Super Sampling is a mobile-optimized upscaling technique that uses machine learning to improve graphical fidelity, while reducing computational cost.

This repository serves as a collection of two plugins to support different Unreal® Engine version, one plugin in folder `UE5.4` supports UE5.4, and the other plugin in folder `UE5.5` supports UE5.5. These two Unreal® Engine plugins provide two different implementations of the Neural Super Sampling technique to be used with Unreal® projects. They all implements the `UE::Renderer::Private::ITemporalUpscaler` interface provided by the engine, so you can integrate the corresponding plugin into your game.

For more details on both of these plugins and how to get started, please see below documents:

* [NSS Plugin for UE5.4](./UE5.4/README.md)
* [NSS Plugin for UE5.5](./UE5.5/README.md)

## Trademarks and Copyrights

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
