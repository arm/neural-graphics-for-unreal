<!--
// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Neural Super Sampling Unreal® Engine Plugin

Neural Super Sampling is a mobile-optimized upscaling technique that uses machine learning to improve graphical fidelity, while reducing computational cost.

This repository provides four plugins, each targeting a specific version of Unreal® Engine. The available plugins are organized as follows:
* UE4.27 — Supports Unreal® Engine 4.27
* UE5.4 — Supports Unreal® Engine 5.4
* UE5.5 — Supports Unreal® Engine 5.5
* UE5.6 — Supports Unreal® Engine 5.6

The plugins for UE4.27, UE5.4, and UE5.6 are implemented using the Neural Graphics SDK for Game Engines, providing a modern and unified framework for temporal upscaling.

The UE5.5 plugin is based on a legacy implementation. Due to outdated design and limited maintenance, this version is not recommended for new integrations.

All plugins implement the engine‑provided UE::Renderer::Private::ITemporalUpscaler interface, enabling you to integrate the appropriate plugin into your game.

For setup instructions and usage details, simply check the README file inside each plugin folder:
* [NSS Plugin for UE4.27](./UE4.27/README.md)
* [NSS Plugin for UE5.4](./UE5.4/README.md)
* [NSS Plugin for UE5.5](./UE5.5/README.md)
* [NSS Plugin for UE5.6](./UE5.6/README.md)

## Trademarks and Copyrights

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.

Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

Vulkan is a registered trademark and the Vulkan SC logo is a trademark of the Khronos Group Inc.

Visual Studio, Windows are registered trademarks or trademarks of Microsoft Corporation in the US and other jurisdictions.
