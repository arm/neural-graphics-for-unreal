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

// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "NGVulkanBackend.h"

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "IVulkanDynamicRHI.h"
#include "Interfaces/IPluginManager.h"
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
#endif

#if WITH_EDITOR && PLATFORM_WINDOWS
		// Ensure the SDK prebuilt binaries can be loaded in Windows Editor
		FString PluginName = TEXT("NSS");
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		if (Plugin.IsValid())
		{
			FString PluginDir = Plugin->GetBaseDir();
			FString Dir = FPaths::Combine(PluginDir, TEXT("Source/NG-SDK/prebuilt_binaries/"));
			FPaths::CollapseRelativeDirectories(Dir);
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

	EFFXBackendAPI GetAPI() const
	{
		return EFFXBackendAPI::Vulkan;
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

	FfxCommandList GetNativeCommandBuffer(FRHICommandListImmediate&, FRHITexture*) final
	{
		check(GetIVulkanDynamicRHI());
		return GetIVulkanDynamicRHI()->RHIGetActiveVkCommandBuffer();
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

	static FfxResource FFXConvertResource(FfxApiResource ApiResource)
	{
		FfxResource Resource;
		FMemory::Memzero(Resource);

		Resource.resource = ApiResource.resource;
		Resource.state = (FfxResourceStates)ApiResource.state;
		memcpy(&Resource.description, &ApiResource.description, sizeof(FfxResourceDescription));

		return Resource;
	}
};

NGVulkanBackend NGVulkanBackend::sNGVulkanBackend;

//-------------------------------------------------------------------------------------
// Implementation for NGVulkanBackendModule.
//-------------------------------------------------------------------------------------
void NGVulkanBackendModule::StartupModule()
{
	if (NGVulkanBackend::sNGVulkanBackend.LoadDLL())
	{
		// We need to enable the Vulkan ML extensions in the Vulkan RHI before it initializes.
		TArray<const ANSICHAR*> Extensions;
		Extensions.Add("VK_ARM_tensors");
		Extensions.Add("VK_ARM_data_graph");
		// These are dependencies of the above extensions
		Extensions.Add("VK_KHR_maintenance5");
		Extensions.Add("VK_KHR_deferred_host_operations");
		Extensions.Add("VK_KHR_synchronization2");

		IVulkanDynamicRHI::AddEnabledDeviceExtensionsAndLayers(Extensions, TArrayView<const ANSICHAR* const>());
	}
	else
	{
		UE_LOG(LogNGVulkanBackend, Error, TEXT("Vulkan Backend failed to load"));
	}
}

void NGVulkanBackendModule::ShutdownModule() {}

INGSharedBackend* NGVulkanBackendModule::GetBackend()
{
	return &NGVulkanBackend::sNGVulkanBackend;
}

#undef LOCTEXT_NAMESPACE
