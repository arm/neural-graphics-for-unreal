# Public v1.1.1 Release Notes

- Update Neural Graphics SDK for Game Engines to v1.1.1.
- Unify Unreal® Engine 5.4 and 5.6 support in a single plugin folder, replacing separate version-specific folders.
- Add `DownloadEmulationLayer.bat` to download the compatible Vulkan ML emulation-layer runtime on Windows. Layer configuration remains a separate setup step.
- Improvements and fixes for Unreal® Engine 5.4 & 5.6 plugins:
  - Improve NSS/NFRU resource lifetime handling and release cached NFRU viewport references during resize.
  - Fix NFRU input handling and NSS history-reset behavior.
  - Fix NFRU rendering artifacts with offset viewports and image-quality issues when `r.ScreenPercentage` is greater than 100.
  - Fix vertically flipped UI after fragment-shader dispatches.
  - Optimize motion-vector conversion to reduce memory bandwidth usage.
  - Fix automatic SDK rebuilding when required binaries are missing.

# Public v1.1.0 Release Notes

- Update Neural Graphics SDK for Game Engines to v1.1.0.
- Use a Vulkan layer provided by SDK to add required extensions.
- New features:
  - Add Neural Frame Rate Upscaling (NFRU) support for Unreal® Engine 5.4 & 5.6 plugins:
    - Support Vulkan® ES3.1, SM5 and SM6.
    - Support Windows and Android platforms.
    - Support fragment-pass for NFRU.
  - Add NSS Android platform support for Unreal® Engine 5.4 & 5.6 plugins:
    - Support fragment-pass for NSS.
    - Support NSS shader quality modes.
- Improvements and fixes for Unreal® Engine 5.4 & 5.6 plugins:
  - Fix a crash when toggling NSS repeatedly.
  - Add deferred deletion for the NSS state.
- Deprecate and remove the Unreal® Engine 5.5 plugin.

# Public v1.0.2 Release Notes

- Added a new folder for Unreal® Engine 5.6 support.
  - Support for Vulkan® ES3.1, SM5 and SM6.
- Code clean up and remove unused code for Unreal® Engine 4.27 & 5.4 plugins.

# Public v1.0.1 Release Notes

- Bug fixes for Unreal® Engine 5.4 plugin:
  - Change r.ScreenPercentage supported range to (0, 100).
  - Set flags to improve performance and add dependent extension.
  - Code clean up and remove unused code.
- Added a new folder for Unreal® Engine 4.27 support.
  - Support for Vulkan® ES3.1 and SM5.

# Public v1.0.0 Release Notes

Arm®'s Neural Super Sampling (NSS) Plugins for Unreal® Engine 5.4 and 5.5 provide two different implementations of the NSS technique to support game developers. UE5.4 plugin is built on top of Neural Graphics SDK for Game Engines, UE5.5 plugin uses Unreal® Neural Network Engine to perform inference of the machine learning model.

## Limitations
This is a Vulkan focused delivery so when using other RHI this plugin will not be enabled for both UE5.4 and UE5.5 version. If you enable this plugin when using other RHI, a warning windows will pop out on the screen.

Please note this plugin is designed for desktop development using ML Emulation Layer for Vulkan for both UE5.4 and UE5.5 version. Thus the runtime performance is not indicative of real device performance.

As part of testing the Neural Upscaler it is recommended that you disable the following features in Unreal, because they can interfere with this technique: Lumen, Motion Blur and Depth of Field.

## Contents
This release package contains the following contents:

UE5.4 folder contains:
### Neural Upscaler
- Unreal® Engine plugin providing Neural Upscaling using the `UE::Renderer::Private::ITemporalUpscaler` interface.
- Support for Unreal® Engine 5.4
- Support for Vulkan® ES3.1, SM5 and SM6

### Nerual Graphics SDK for Game Engines v1.0.0
- Nerual Graphics SDK for Game Engines is degined to providing multiple rendering use cases across diverse engines and platforms. This release only supports NSS use case.
- Compatible with FidelityFX API 1.1.3
- Compiled for x64 Windows 11
- Source code provided, you can also download other version SDK from https://github.com/arm/neural-graphics-sdk-for-game-engines, but the compatibilty with this plugin may not be verified.

### Arm ML Extensions for Vulkan® v0.7.0
- Precompiled binaries of the ML Emulation Layer for Vulkan®.
- Compiled for x64 Windows 11
- Binary provided, you can also download it from https://github.com/arm/ai-ml-emulation-layer-for-vulkan/releases

UE5.5 folder contains:
### Neural Upscaler
- Unreal® Engine plugin providing Neural Upscaling using the `UE::Renderer::Private::ITemporalUpscaler` interface.
- Support for Unreal® Engine 5.5
- Support for Vulkan® ES3.1, SM5 and SM6

More information about NSS plugins for Unreal® Engine can be found in the [README.md](./README.md).
