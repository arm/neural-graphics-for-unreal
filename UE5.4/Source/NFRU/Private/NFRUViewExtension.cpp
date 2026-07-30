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

#include "NFRUViewExtension.h"
#include "LogNFRU.h"
#include "NFRU.h"
#include "PostProcess/PostProcessMobile.h"
#include "PostProcess/PostProcessing.h"
#include "ScenePrivate.h"

NFRUViewExtension::NFRUViewExtension(const FAutoRegister& AutoRegister, NFRU* InFrameInterpolation)
	: FSceneViewExtensionBase(AutoRegister)
	, FrameInterpolation(InFrameInterpolation)
	, bFrameInterpolationSupported(true)
{
	// Main gating is done in NFRU::OnPostEngineInit.
	// This constructor should only be called when Vulkan RHI is active.
	if (GDynamicRHI && FString(GDynamicRHI->GetName()) != TEXT("Vulkan"))
	{
		UE_LOG(LogNFRU,
			Warning,
			TEXT("NFRUViewExtension created with non-Vulkan RHI (%s). Frame interpolation is disabled."),
			GDynamicRHI->GetName());
		bFrameInterpolationSupported = false;
	}
}

void NFRUViewExtension::PrePostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	if (View.GetFeatureLevel() >= ERHIFeatureLevel::SM5 && bFrameInterpolationSupported)
	{
		FrameInterpolation->SetupView(View, Inputs);
	}
}

void NFRUViewExtension::PrePostProcessPassMobile_RenderThread(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FMobilePostProcessingInputs& Inputs)
{
	if (View.GetFeatureLevel() == ERHIFeatureLevel::ES3_1 && bFrameInterpolationSupported)
	{
		FrameInterpolation->SetupView(View, Inputs);
	}
}
