<!-- 
// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
-->

# Enchanted Castle

## Overview

**Enchanted Castle** is an Unreal® Engine demo project designed to showcase two mobile-optimized temporal upscaling techniques: **Arm® Accuracy Super Resolution™ (Arm ASR)** and **Neural Super Sampling (NSS)**.

The plugins required for these techniques are not included in this repository and must be installed separately.

## Requirements

- Unreal® Engine 5.4 or later  
- **Arm ASR** and **NSS** each define their own requirements. Please refer to their GitHub repositories for details.

## Installation

1. Install the plugins:

   - [Arm® Accuracy Super Resolution™ (Arm ASR) Unreal® Engine Plugin](https://github.com/arm/accuracy-super-resolution-for-unreal)  
     Refer to the plugin's README for installation instructions.

   - [Neural Super Sampling (NSS) Unreal® Engine Plugin](https://github.com/arm/neural-graphics-for-unreal)  
     Refer to the [Neural Super Sampling in Unreal Engine](https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/) for installation instructions.

2. Open the project in Unreal® Engine. The editor will detect and compile the plugins automatically.

## Usage

Play the default level. Click anywhere in the game viewport to toggle the control menu, where you can switch sequences, change anti-aliasing settings, adjust the screen percentage, and enable or disable the debug overlay.

## License

This project (except for the LTween plugin) is licensed under the [MIT License](LICENSES/MIT.txt).

[LTween](https://www.fab.com/listings/1dbb1791-152d-4581-8c0e-32faccabfbf2) is an Unreal® Engine plugin developed by [LexLiu](https://github.com/liufei2008).  
It is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) and has not been modified.

The external plugins, **Arm ASR** and **NSS**, are distributed under their own licenses.  
Please refer to their GitHub repositories for details.

## Trademarks and Copyright

Arm® is a registered trademark of Arm Limited (or its subsidiaries) in the US and/or elsewhere.  
Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.

## Contact

For questions or feedback, please reach out via the GitHub repository issue tracker of this project or the respective plugin repositories.  
For security issues, please refer to the security policies provided in the respective plugin repositories.
