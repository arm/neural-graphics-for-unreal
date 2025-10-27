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

#pragma once

#include "Modules/ModuleManager.h"
#include "NGShared.h"
#include "RHIFwd.h"

class FRDGTexture;
enum EPixelFormat : uint8;
enum class ERHIAccess : uint32;

enum class EFFXBackendAPI : uint8
{
	Vulkan,
	Unsupported,
	Unknown
};

struct NGSharedAllocCallbacks
{
	static void* ffxAlloc(void* pUserData, uint64_t size)
	{
		return FMemory::Malloc(size);
	}

	static void ffxDealloc(void* pUserData, void* pMem)
	{
		return FMemory::Free(pMem);
	}

	ffxAllocationCallbacks Cbs;

	NGSharedAllocCallbacks()
	{
		Cbs.pUserData = nullptr;
		Cbs.alloc = &ffxAlloc;
		Cbs.dealloc = &ffxDealloc;
	}
};

class FRDGBuilder;

class INGSharedBackend
{
public:
	virtual ffxReturnCode_t ffxCreateContext(ffxContext* context, ffxCreateContextDescHeader* desc) = 0;
	virtual ffxReturnCode_t ffxDestroyContext(ffxContext* context) = 0;
	virtual ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc) = 0;
	virtual ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc) = 0;
	virtual ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc) = 0;

	virtual EFFXBackendAPI GetAPI() const = 0;
	virtual FfxApiResource GetNativeResource(FRHITexture* Texture, FfxApiResourceState State) = 0;
	virtual FfxApiResource GetNativeResource(FRDGTexture* Texture, FfxApiResourceState State) = 0;
	virtual FfxCommandList GetNativeCommandBuffer(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture) = 0;
	virtual bool IsNeuralGraphicSupported() = 0;
	virtual bool IsLoaded() = 0;
	virtual void ForceUAVTransition(
		FRHICommandListImmediate& RHICmdList, FRHITexture* OutputTexture, ERHIAccess Access) = 0;
};

class INGSharedBackendModule : public IModuleInterface
{
public:
	virtual INGSharedBackend* GetBackend() = 0;
};
