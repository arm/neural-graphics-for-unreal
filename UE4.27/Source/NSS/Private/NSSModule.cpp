// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All rights reserved.
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

#include "NSSModule.h"

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "LogNSS.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "NSS.h"
#include "NSSViewExtension.h"

IMPLEMENT_MODULE(NSSModule, NSS)

#define LOCTEXT_NAMESPACE "NSS"

DEFINE_LOG_CATEGORY(LogNSS);

static bool GNSSModuleInit = false;

static bool IsRunningCookCommandlet()
{
	FString Commandline = FCommandLine::Get();
	const bool bIsCookCommandlet = IsRunningCommandlet() && Commandline.Contains(TEXT("run=cook"));
	return bIsCookCommandlet;
}

void NSSModule::StartupModule()
{
	FString PluginShaderDir =
		FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NSS"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/NSS"), PluginShaderDir);

	if (IsRunningCookCommandlet()) // Note this check needs to come after the shader source mapping, otherwise crashy
	{
		// If cooking, we won't have an RHI and can't use this plugin. The RHI check below would catch this, but will
		// report an error which will fail the cooking commandlet. Instead we detect cooking separately and log this at
		// a lower severity.
		UE_LOG(LogNSS, Log, TEXT("Cooking detected - the ArmNG NSS Module will not be available."));
		return;
	}

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &NSSModule::OnPostEngineInit);
	GNSSModuleInit = true;
	UE_LOG(LogNSS, Log, TEXT("NSS Temporal Upscaling Module Started"));
}

void NSSModule::ShutdownModule()
{
	GNSSModuleInit = false;
	UE_LOG(LogNSS, Log, TEXT("NSS Temporal Upscaling Module Shutdown"));
}

bool NSSModule::IsInitialized()
{
	return GNSSModuleInit;
}

void NSSModule::SetTemporalUpscaler(TSharedPtr<NSS, ESPMode::ThreadSafe> Upscaler)
{
	TemporalUpscaler = Upscaler;
}

void NSSModule::OnPostEngineInit()
{
	if (GDynamicRHI)
	{
		FString RHIName = GDynamicRHI->GetName();

		if (RHIName == TEXT("Vulkan"))
		{
			INGSharedBackendModule* VkBackend =
				FModuleManager::GetModulePtr<INGSharedBackendModule>(TEXT("NGVulkanBackend"));
			INGSharedBackend* ApiAccessor = VkBackend ? VkBackend->GetBackend() : nullptr;
			if (ApiAccessor && ApiAccessor->IsLoaded())
			{
				ViewExtension = FSceneViewExtensions::NewExtension<NSSViewExtension>();
			}
			else
			{
#if PLATFORM_WINDOWS
				// Prompt the user with more detailed information to help them debug the issue.
				FString BaseDir = FPlatformProcess::BaseDir();
				FString DllPath = FPaths::Combine(BaseDir, TEXT("ngsdk_windows_x64.dll"));
				bool bDllExists = FPaths::FileExists(DllPath);

				FString Msg = FString::Printf(TEXT("Failed to load the SDK DLL.\n\n"
												   "Check the following:\n"
												   "  DLL staged to: %s\n"
												   "  DLL present: %s\n"),
					*DllPath,
					bDllExists ? TEXT("YES") : TEXT("NO"));

				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
				UE_LOG(LogNSS, Error, TEXT("%s"), *Msg);
				return;
#endif
			}
		}
		else
		{
			FString Msg =
				TEXT("Plugin only supports Vulkan backend. Please switch to Vulkan and restart the editor/game.");
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
			UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
			return;
		}
	}
}

NSS* NSSModule::GetNSSUpscaler() const
{
	return TemporalUpscaler.Get();
}

INSS* NSSModule::GetTemporalUpscaler() const
{
	return TemporalUpscaler.Get();
}

bool NSSModule::IsPlatformSupported(EShaderPlatform Platform) const
{
	FStaticShaderPlatform ShaderPlatform(Platform);

	return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::ES3_1);
}

void NSSModule::SetEnabledInEditor(bool bEnabled)
{
#if WITH_EDITOR
	return TemporalUpscaler->SetEnabledInEditor(bEnabled);
#endif
}

#undef LOCTEXT_NAMESPACE
