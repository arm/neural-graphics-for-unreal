// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (c) 2023-2024 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "NGVulkanBackend.h"

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "IVulkanDynamicRHI.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"
#include "NGSharedBackend.h"
#include "NGVulkanIncludes.h"
#include "RenderGraphResources.h"
#include "VulkanRHIPrivate.h"

#define LOCTEXT_NAMESPACE "NGVulkanBackend"

DECLARE_LOG_CATEGORY_EXTERN(LogNGVulkanBackend, Verbose, All);
DEFINE_LOG_CATEGORY(LogNGVulkanBackend);

//-------------------------------------------------------------------------------------
// Helper variable declarations.
//-------------------------------------------------------------------------------------
IMPLEMENT_MODULE(NGVulkanBackendModule, NGVulkanBackend)

//-------------------------------------------------------------------------------------
// The Vulkan implementation of the FFX shared backend that interacts with the VulkanRHI.
//-------------------------------------------------------------------------------------
class NGVulkanBackend : public INGSharedBackend
{
	NGSharedAllocCallbacks AllocCbs;
	ffxFunctions FfxFunctions;
	void* FfxModule;
	static inline bool bLoaded = false;
	static inline bool bVulkanRHI = true;

public:
	static NGVulkanBackend sNGVulkanBackend;

	NGVulkanBackend()
	{
		FMemory::Memzero(FfxFunctions);
		FfxModule = nullptr;
	}

	virtual ~NGVulkanBackend()
	{
		if (FfxModule)
		{
			FPlatformProcess::FreeDllHandle(FfxModule);
		}
	}

	bool IsLoaded()
	{
		return bLoaded;
	}

	bool LoadDLL()
	{
		bool bOk = false;

		FString Name;
#if PLATFORM_WINDOWS
		Name = TEXT("ngsdk_windows_x64.dll");
#elif PLATFORM_LINUX
#if PLATFORM_CPU_ARM_FAMILY
		Name = TEXT("libngsdk_linux_arm.so");
#else
		Name = TEXT("libngsdk_linux_x64.so");
#endif
#elif PLATFORM_ANDROID
		Name = TEXT("libngsdk_android_arm.so");
#endif

#if PLATFORM_WINDOWS && WITH_EDITOR
		// Ensure the SDK prebuilt binaries can be loaded in Windows Editor
		FString PluginName = TEXT("NG");
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);

		if (Plugin.IsValid())
		{
			FString PluginDir = Plugin->GetBaseDir();
			FString Dir = FPaths::Combine(PluginDir, TEXT("Binaries/ThirdParty/PreBuiltNGSDK/"));
			Dir = FPaths::ConvertRelativePathToFull(Dir);
			FPlatformProcess::AddDllDirectory(*Dir);
			Name = FPaths::Combine(Dir, Name);
		}
		else
		{
			UE_LOG(LogNGVulkanBackend, Error, TEXT("Plugin %s not found!"), *PluginName);
		}
#endif

		FfxModule = FPlatformProcess::GetDllHandle(*Name);
		if (FfxModule)
		{
			ffxLoadFunctions(&FfxFunctions, (FfxModuleHandle)FfxModule);
			bOk = FfxFunctions.CreateContext ? true : false;
		}
		bLoaded = bOk;
		return bOk;
	}

	ffxReturnCode_t ffxCreateContext(ffxContext* context, ffxCreateContextDescHeader* desc) final
	{
		ffxCreateBackendVKDesc VulkanHeader = {};
		VulkanHeader.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
		VulkanHeader.header.pNext = nullptr;
		VulkanHeader.vkDevice = GetIVulkanDynamicRHI()->RHIGetVkDevice();
		VulkanHeader.vkPhysicalDevice = GetIVulkanDynamicRHI()->RHIGetVkPhysicalDevice();
		VulkanHeader.vkInstance = GetIVulkanDynamicRHI()->RHIGetVkInstance();
		VulkanHeader.vkGetInstanceProcAddr =
			(PFN_vkGetInstanceProcAddr)GetIVulkanDynamicRHI()->RHIGetVkInstanceProcAddr("vkGetInstanceProcAddr");
		VulkanHeader.vkDeviceProcAddr =
			(PFN_vkGetDeviceProcAddr)GetIVulkanDynamicRHI()->RHIGetVkDeviceProcAddr("vkGetDeviceProcAddr");
		desc->pNext = (ffxApiHeader*)&VulkanHeader;

		ffxReturnCode_t ret = FfxFunctions.CreateContext(context, desc, &AllocCbs.Cbs);
		return ret;
	}

	ffxReturnCode_t ffxDestroyContext(ffxContext* context) final
	{
		return FfxFunctions.DestroyContext(context, &AllocCbs.Cbs);
	}

	ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc) final
	{
		return FfxFunctions.Configure(context, desc);
	}

	ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc) final
	{
		return FfxFunctions.Query(context, desc);
	}

	ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) final
	{
		return FfxFunctions.Dispatch(context, desc);
	}

	void Init() final {}

	EFFXBackendAPI GetAPI() const
	{
		return EFFXBackendAPI::Vulkan;
	}

	FfxSwapchain GetSwapchain(void* swapChain)
	{
		return (FfxSwapchain)swapChain;
	}

	uint32 GetNativeTextureFormat(FRHITexture* Texture)
	{
		check(Texture);
		check(Texture->GetTexture2D());
		return (uint32)((FVulkanTexture*)(Texture->GetTexture2D()))->ViewFormat;
	}

	FfxApiResource GetNativeResource(FRHITexture* Texture, FfxApiResourceState State) final
	{
		check(Texture);
		checkf(Texture->GetDesc().Dimension == ETextureDimension::Texture2D,
			TEXT("NGVulkanBackend only supports Texture2D for now!"));

		FIntVector size = Texture->GetSizeXYZ();
		const auto imgFormat = (VkFormat)GetNativeTextureFormat(Texture);

		FfxApiResource resource = {};
		resource.resource = Texture->GetNativeResource();
		resource.state = State;
		resource.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
		resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
		resource.description.width = size.X;
		resource.description.height = size.Y;
		resource.description.depth = 1;
		resource.description.mipCount = Texture->GetNumMips();
		resource.description.format = ffxApiGetSurfaceFormatVK(imgFormat);
		resource.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;

		// Check for depth stencil use
		if (ffxApiIsDepthFormat(imgFormat))
		{
			resource.description.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;
		}
		if (ffxApiIsStencilFormat(imgFormat))
		{
			resource.description.usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;
		}
		if (State & FFX_API_RESOURCE_STATE_RENDER_TARGET)
		{
			resource.description.usage |= FFX_API_RESOURCE_USAGE_RENDERTARGET;
		}
		if (State & FFX_API_RESOURCE_STATE_UNORDERED_ACCESS)
		{
			resource.description.usage |= FFX_API_RESOURCE_USAGE_UAV;
		}

		return resource;
	}

	FfxApiResource GetNativeResource(FRDGTexture* Texture, FfxApiResourceState State) final
	{
		check(Texture);
		return GetNativeResource(Texture->GetRHI(), State);
	}

	FfxCommandList GetNativeCommandBuffer(FRHICommandListImmediate&) final
	{
		check(GetIVulkanDynamicRHI());
		return GetIVulkanDynamicRHI()->RHIGetActiveVkCommandBuffer();
	}

	bool EnsureSupportedRHI() final
	{
		return bVulkanRHI;
	}

	void PostEngineInit()
	{
		bVulkanRHI = !GDynamicRHI || FString(GDynamicRHI->GetName()) == TEXT("Vulkan");
		if (!bVulkanRHI)
		{
			const FString RHIName = GDynamicRHI->GetName();
			const FString Msg = FString::Printf(
				TEXT("Arm Neural Graphics plugin is disabled: it requires the Vulkan RHI but the active RHI is '%s'.\n"
					 "\nTo enable it, go to:\n"
					 "    Edit -> Project Settings -> Platforms -> Windows -> Default RHI\n"
					 "and set it to 'Vulkan', then restart the editor."),
				*RHIName);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
			UE_LOG(LogNGVulkanBackend, Warning, TEXT("%s"), *Msg);
		}
	}

	bool IsNeuralGraphicSupported()
	{
		static bool tensorSupported = false;
		static bool dataGraphSupported = false;
		static bool checked = false;
		if (checked)
		{
			return tensorSupported && dataGraphSupported;
		}
		if (!bVulkanRHI)
		{
			return false;
		}
		IVulkanDynamicRHI* vulkanRHI = GetIVulkanDynamicRHI();
		check(vulkanRHI);
		VkPhysicalDevice vkPhysicalDevice = vulkanRHI->RHIGetVkPhysicalDevice();
		check(vkPhysicalDevice);
		TArray<VkExtensionProperties> extensions = vulkanRHI->RHIGetAllDeviceExtensions(vkPhysicalDevice);

		for (const VkExtensionProperties& ext : extensions)
		{
			if (strcmp(ext.extensionName, "VK_ARM_tensors") == 0)
			{
				tensorSupported = true;
			}
			else if (strcmp(ext.extensionName, "VK_ARM_data_graph") == 0)
			{
				dataGraphSupported = true;
			}
		}
		checked = true;
		return tensorSupported && dataGraphSupported;
	}

	void ForceUAVTransition(FRHICommandListImmediate& RHICmdList, FRHITexture* OutputTexture, ERHIAccess Access)
	{
		FRHITransitionInfo Info(OutputTexture, ERHIAccess::Unknown, Access);
		RHICmdList.Transition(Info);
	}

	void Flush(FRHICommandListImmediate& RHICmdList) final
	{
		// Keep the external compute completion notification ordered after the
		// queued SDK dispatch commands.
		RHICmdList.EnqueueLambda(
			[this](FRHICommandListImmediate& cmd)
			{
				IVulkanDynamicRHI* DynamicRHI = GetIVulkanDynamicRHI();
				DynamicRHI->RHIFinishExternalComputeWork(static_cast<VkCommandBuffer>(GetNativeCommandBuffer(cmd)));
			});
	}

	void RestoreGraphicsStateAfterExternalWork(FRHICommandListImmediate& RHICmdList, const FIntRect& RestoreRect) final
	{
		check(RestoreRect.Min.X >= 0 && RestoreRect.Min.Y >= 0);
		check(RestoreRect.Max.X > RestoreRect.Min.X && RestoreRect.Max.Y > RestoreRect.Min.Y);

		// FFX fragment jobs modify Vulkan graphics state directly. Restore the UE
		// viewport convention after the queued SDK commands, then use the existing
		// external-work completion API to reset UE's pending graphics state.
		RHICmdList.EnqueueLambda(
			[this, RestoreRect](FRHICommandListImmediate& cmd)
			{
				IVulkanDynamicRHI* DynamicRHI = GetIVulkanDynamicRHI();
				VkCommandBuffer CommandBuffer = static_cast<VkCommandBuffer>(GetNativeCommandBuffer(cmd));
				const FIntPoint Extent = RestoreRect.Size();

				VkViewport Viewport{};
				Viewport.x = static_cast<float>(RestoreRect.Min.X);
				Viewport.y = static_cast<float>(RestoreRect.Max.Y);
				Viewport.width = static_cast<float>(Extent.X);
				Viewport.height = -static_cast<float>(Extent.Y);
				Viewport.minDepth = 0.0f;
				Viewport.maxDepth = 1.0f;
				VulkanRHI::vkCmdSetViewport(CommandBuffer, 0, 1, &Viewport);

				VkRect2D Scissor{};
				Scissor.offset.x = RestoreRect.Min.X;
				Scissor.offset.y = RestoreRect.Min.Y;
				Scissor.extent.width = static_cast<uint32>(Extent.X);
				Scissor.extent.height = static_cast<uint32>(Extent.Y);
				VulkanRHI::vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

				DynamicRHI->RHIFinishExternalComputeWork(CommandBuffer);
			});
	}
};

NGVulkanBackend NGVulkanBackend::sNGVulkanBackend;

//-------------------------------------------------------------------------------------
// Implementation for NGVulkanBackendModule.
//-------------------------------------------------------------------------------------
void NGVulkanBackendModule::StartupModule()
{
	const FString PluginBaseDir = IPluginManager::Get().FindPlugin(TEXT("NG"))->GetBaseDir();

#if PLATFORM_WINDOWS
#if WITH_EDITOR
	const FString LayerDirectory = FPaths::Combine(PluginBaseDir, TEXT("Binaries/ThirdParty/PreBuiltNGSDK"));
#else
	const FString LayerDirectory = FPlatformProcess::BaseDir();
#endif
	const FString LayerManifest = FPaths::Combine(LayerDirectory, TEXT("VkLayer_arm_NG_windows.json"));
	if (FPaths::FileExists(LayerManifest))
	{
		// VK_LAYER_PATH overrides VK_ADD_LAYER_PATH; UE uses it in debug builds without a Vulkan SDK.
		bool bUseLayerPath = !FPlatformMisc::GetEnvironmentVariable(TEXT("VK_LAYER_PATH")).IsEmpty();
#if VULKAN_HAS_DEBUGGING_ENABLED
		bUseLayerPath |= FPlatformMisc::GetEnvironmentVariable(TEXT("VULKAN_SDK")).IsEmpty();
#endif
		const TCHAR* Variable = bUseLayerPath ? TEXT("VK_LAYER_PATH") : TEXT("VK_ADD_LAYER_PATH");
		FString SearchPaths = FPlatformMisc::GetEnvironmentVariable(Variable);
		if (SearchPaths.IsEmpty())
		{
			SearchPaths = FPlatformMisc::GetEnvironmentVariable(TEXT("VK_ADD_LAYER_PATH"));
		}
		// NG must precede Graph/Tensor emulation to enable the advertised features before device creation.
		const FString FullDirectory = FPaths::ConvertRelativePathToFull(LayerDirectory);
		SearchPaths = FullDirectory + (SearchPaths.IsEmpty() ? FString() : TEXT(";") + SearchPaths);
		FPlatformMisc::SetEnvironmentVar(Variable, *SearchPaths);
	}
	else
	{
		UE_LOG(LogNGVulkanBackend, Warning, TEXT("NG Vulkan layer manifest not found: %s"), *LayerManifest);
	}
#endif

	// Register shader source directory mapping for the NG shaders
	FString PluginShaderDir = FPaths::Combine(PluginBaseDir, TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/NG"), PluginShaderDir);

	// Check that the Vulkan RHI is active after the engine has fully initialized.
	FCoreDelegates::OnPostEngineInit.AddLambda([]() { NGVulkanBackend::sNGVulkanBackend.PostEngineInit(); });

	if (NGVulkanBackend::sNGVulkanBackend.LoadDLL())
	{
		// We need to enable the ML extensions for Vulkan in the Vulkan RHI before it initializes.
		TArray<const ANSICHAR*> Extensions;
		Extensions.Add("VK_ARM_tensors");
		Extensions.Add("VK_ARM_data_graph");
		Extensions.Add("VK_ARM_data_graph_optical_flow");
		// These are dependencies of the above extensions
		Extensions.Add("VK_KHR_maintenance5");
		Extensions.Add("VK_KHR_deferred_host_operations");
		Extensions.Add("VK_KHR_synchronization2");

		// Android: The Vulkan loader (Android API 28+) discovers layers matching libVkLayer_*.so
		// in the app's nativeLibraryDir automatically — no UE source modification needed.
		// Windows / Linux: register the Neural Graphics feature layer by name.
		// Windows configures its manifest search path above. Linux still requires
		// VK_LAYER_PATH to include the manifest staged alongside the game binary.
		static const ANSICHAR* Layers[] = {"VK_LAYER_ARM_NG"};
		IVulkanDynamicRHI::AddEnabledInstanceExtensionsAndLayers(
			TArrayView<const ANSICHAR* const>(), TArrayView<const ANSICHAR* const>(Layers, 1));

		IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers(Extensions, TArrayView<const ANSICHAR* const>());
	}
	else
	{
		UE_LOG(LogNGVulkanBackend, Error, TEXT("Vulkan Backend failed to load"));
#if PLATFORM_WINDOWS
		if (!IsRunningCookCommandlet())
		{
			// Prompt the user with more detailed information to help them debug the issue.
			FString BaseDir = FPlatformProcess::BaseDir();
			FString DllPath = FPaths::Combine(BaseDir, TEXT("ngsdk_windows_x64.dll"));
			bool bDllExists = FPaths::FileExists(DllPath);
			// clang-format off
			FString Msg = FString::Printf(
				TEXT("Required runtime dependency for ngSDK is not available.\n\n")
				TEXT("Checked DLL path: %s\n") TEXT("DLL exists: %s\n"),
				*DllPath,
				bDllExists ? TEXT("YES") : TEXT("NO")
			);
			// clang-format on
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
		}
#endif
	}
}

void NGVulkanBackendModule::ShutdownModule() {}

INGSharedBackend* NGVulkanBackendModule::GetBackend()
{
	return &NGVulkanBackend::sNGVulkanBackend;
}

#undef LOCTEXT_NAMESPACE
