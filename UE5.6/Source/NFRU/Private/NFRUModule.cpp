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

// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#include "NFRUModule.h"
#include "LogNFRU.h"
#include "NFRU.h"

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#if UE_VERSION_AT_LEAST(5, 1, 0)
#include "Misc/ConfigUtilities.h"
#endif

IMPLEMENT_MODULE(NFRUModule, NFRU)

#define LOCTEXT_NAMESPACE "NFRU"

DEFINE_LOG_CATEGORY(LogNFRU);

void NFRUModule::StartupModule()
{
	// Ensure NG Vulkan backend is loaded
	FModuleManager::Get().LoadModuleChecked("NGVulkanBackend");

	if (IsRunningCookCommandlet()) // Note this check needs to come after the shader source mapping, otherwise crashy
	{
		// If cooking, we won't have an RHI and can't use this plugin. The RHI check below would catch this, but will
		// report an error which will fail the cooking commandlet. Instead we detect cooking separately and log this at
		// a lower severity.
		return;
	}

	Impl = new NFRU;
	UE_LOG(LogNFRU, Log, TEXT("NFRU Module Started"));
}

void NFRUModule::ShutdownModule()
{
	delete Impl;
	UE_LOG(LogNFRU, Log, TEXT("NFRU Module Shutdown"));
}

INFRU* NFRUModule::GetImpl()
{
	return Impl;
}
