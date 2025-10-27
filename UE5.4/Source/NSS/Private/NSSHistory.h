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

#include "CoreMinimal.h"
#include "INSSHistory.h"
#include "NSSInclude.h"
#include "SceneRendering.h"

class NSS;

//-------------------------------------------------------------------------------------
// The NSS state wrapper, deletion is handled by the RHI so that they aren't removed out from under the GPU.
//-------------------------------------------------------------------------------------
struct NSSState : public FRHIResource
{
	NSSState(INGSharedBackend* InBackend) : FRHIResource(RRT_None), Backend(InBackend), LastUsedFrame(~0u)
	{
		check(InBackend);
	}

	~NSSState()
	{
		if (Backend != nullptr)
		{
			Backend->ffxDestroyContext(&Nss);
		}
	}

	uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}

	uint32 Release() const
	{
		return FRHIResource::Release();
	}

	uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}

	INGSharedBackend* Backend;
	ffxApiCreateContextDescNss Params;
	ffxContext Nss;
	uint64 LastUsedFrame;
	uint32 ViewID;
};
typedef TRefCountPtr<NSSState> NSSStateRef;

//-------------------------------------------------------------------------------------
// The ICustomTemporalAAHistory for NSS, this retains the NSS state object.
//-------------------------------------------------------------------------------------
class NSSHistory final : public INSSHistory, public FRefCountBase
{
public:
	NSSHistory(NSSStateRef NewState, NSS* _Upscaler);

	virtual ~NSSHistory();

	virtual const TCHAR* GetDebugName() const override;
	virtual uint64 GetGPUSizeBytes() const override;

	ffxContext* GetNSSContext() const final;
	ffxApiCreateContextDescNss* GetNSSContextDesc() const final;

	void SetState(NSSStateRef NewState);

	inline NSSStateRef const& GetState() const
	{
		return Nss;
	}

	uint32 AddRef() const final
	{
		return FRefCountBase::AddRef();
	}

	uint32 Release() const final
	{
		return FRefCountBase::Release();
	}

	uint32 GetRefCount() const final
	{
		return FRefCountBase::GetRefCount();
	}

	static TCHAR const* GetUpscalerName();

	// We need to keep these around on the application side instead of FFX side as otherwise we'd have to blit
	// each of these into an internal resource. Instead, we can simply keep them alive here on the app side through RDG.
	TRefCountPtr<IPooledRenderTarget> PaddedUpscaledColour; // No view rect associated here - always the full thing
	TRefCountPtr<IPooledRenderTarget> PaddedDepth; // View rect is specified by PaddedDepthViewRect
	FIntRect PaddedDepthViewRect; // Might be smaller than the texture extent (e.g. tiling quantisation)

private:
	static TCHAR const* FfxNssDebugName;
	NSSStateRef Nss;
	NSS* Upscaler;
};
