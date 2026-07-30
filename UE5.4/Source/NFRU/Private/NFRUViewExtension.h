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

#pragma once

#include "Misc/EngineVersionComparison.h"
#include "SceneViewExtension.h"

// Forward declaration for mobile post processing
struct FMobilePostProcessingInputs;

class NFRU;

//------------------------------------------------------------------------------------------------------
// View extension that inserts frame interpolation.
//------------------------------------------------------------------------------------------------------
class NFRUViewExtension final : public FSceneViewExtensionBase
{
	NFRU* FrameInterpolation;
	bool bFrameInterpolationSupported;

public:
	NFRUViewExtension(const FAutoRegister& AutoRegister, NFRU* InFrameInterpolation);

	// ISceneViewExtension interface
	void SetupViewFamily(FSceneViewFamily& InViewFamily) final {}
	void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) final {}
	void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) final {}
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) final {}
	void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) final {}
	void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) final
	{}
#endif
	void PrePostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) final;

	void PrePostProcessPassMobile_RenderThread(
		FRDGBuilder& GraphBuilder, const FSceneView& View, const FMobilePostProcessingInputs& Inputs);
};
