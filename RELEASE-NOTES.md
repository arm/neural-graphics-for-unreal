## Public v1.0.0 Release Notes

Arm®'s Neural Super Sampling (NSS) Plugins for Unreal® Engine 5.4 and 5.5 provide two different implementations of the NSS technique to support game developers. UE5.4 plugin is built on top of Neural Graphics SDK for Game Engines, UE5.5 plugin uses Unreal® Neural Network Engine to perform inference of the machine learning model.

## Limitations
This is a Vulkan focused delivery so when using DirectX12 RHI this plugin will not be enabled for both UE5.4 and UE5.5 version. If you enable this plugin when using DirectX12 RHI, a warning windows will pop out on the screen.

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
