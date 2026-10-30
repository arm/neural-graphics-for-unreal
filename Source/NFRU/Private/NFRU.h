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

#include "Engine/Engine.h"
#include "HAL/PlatformFramePacer.h"
#include "INFRU.h"
#include "LogNFRU.h"
#include "SceneViewExtension.h"

//-------------------------------------------------------------------------------------
// Forward declarations.
//-------------------------------------------------------------------------------------
class FRDGBuilder;
class SWindow;
class FViewport;
class NFRUViewExtension;
class NFRUCustomPresent;
struct FfxFrameInterpolationContext;

//-------------------------------------------------------------------------------------
// Class that actually implements frame interpolation.
//-------------------------------------------------------------------------------------
class NFRU : public INFRU
{
public:
	NFRU();
	virtual ~NFRU();

	void OnPostEngineInit();
	void OnViewportCreatedHandler_SetCustomPresent();
	void OnBeginDrawHandler();
	template <typename TInputs> void SetupView(const FSceneView& View, const TInputs& Inputs);
	void InterpolateFrame(FRDGBuilder& GraphBuilder);
	void OnSlateWindowRendered(SWindow& SlateWindow, void* ViewportRHIPtr);
	void OnPreResizeWindowBackBufferCallback(void* ViewportRHIPtr);
	void OnPostResizeWindowBackBufferCallback(void* ViewportRHIPtr);
#if UE_VERSION_AT_LEAST(5, 5, 0)
	void OnBackBufferReadyToPresentCallback(class SWindow& SlateWindow, const FTextureRHIRef& BackBuffer);
#else
	void OnBackBufferReadyToPresentCallback(class SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
#endif

	INFRUCustomPresent* CreateCustomPresent(INGSharedBackend* Backend,
		uint32_t Flags,
		FIntPoint RenderSize,
		FIntPoint DisplaySize,
		FfxSwapchain RawSwapChain,
		FfxCommandQueue Queue,
		FfxApiSurfaceFormat Format,
		EFFXBackendAPI Api) final;
	bool GetAverageFrameTimes(float& AvgTimeMs, float& AvgFPS) final;
	void ResetFPS(IConsoleVariable* Vcar);
	void RegisterCVarCallbacks();

private:
	struct NFRUView
	{
		FRDGTextureRef ViewFamilyTexture;
		FRDGTextureRef SceneDepth;
		FRDGTextureRef SceneVelocity;
		FIntRect ViewRect;
		FIntPoint InputExtentsQuantized;
		FIntPoint OutputExtents;
		FVector2D TemporalJitterPixels;
		float CameraNear;
		float CameraFOV;
		float GameTimeMs;
		bool bReset;
		bool bEnabled;
	};
	void CalculateFPSTimings();
	bool InterpolateView(FRDGBuilder& GraphBuilder,
		NFRUCustomPresent* Presenter,
		const FSceneView* View,
		NFRUView const& ViewDesc,
		FRDGTextureRef FinalBuffer,
		FRDGTextureRef ColorBufferTm1,
		FRDGTextureRef InterpolatedRDG,
		FRDGTextureRef BackBufferRDG,
		uint32 Index);
	TMap<const FSceneView*, NFRUView> Views;
	TSharedPtr<NFRUViewExtension, ESPMode::ThreadSafe> ViewExtension;
	// this is ping-pong to save one copy.
	TRefCountPtr<IPooledRenderTarget> BackBufferRT[2];
	TRefCountPtr<IPooledRenderTarget> InterpolatedRT;
	TRefCountPtr<IPooledRenderTarget> AsyncBufferRT[2];
	TMap<FfxSwapchain, NFRUCustomPresent*> SwapChains;
	TMap<SWindow*, FViewportRHIRef> Windows;
	// Non-owning slot associations retained only while cached viewport references are released for resize.
	TMap<SWindow*, void*> ResizingWindows;
	float GameDeltaTime;
	double LastTime;
	float AverageTime;
	float AverageFPS;
	uint64 InterpolationCount;
	uint64 PresentCount;
	uint32 Index;
	uint32 ResetState;
	bool bInterpolatedFrame;
	bool bInitVelocityCacheState = false;
	bool bLastVelocityCacheState = false;
	bool bLastUseFragmentShader = false;
	bool bResetFPS = true;
	int32 DefaultFPS = FPlatformRHIFramePacer::GetFramePace();
	int32 LastFPS = FPlatformRHIFramePacer::GetFramePace();
};
