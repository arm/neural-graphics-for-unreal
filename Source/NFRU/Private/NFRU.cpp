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

#ifndef FFX_CPU
#define FFX_CPU
#endif

#include "NFRU.h"
#include "FFXRDGBuilder.h"
#include "FidelityFX/host/ffx_frameinterpolation_resolution.h"
#include "FidelityFX/host/ffx_interface.h"
#include "NFRUCustomPresent.h"
#include "NFRUSlate.h"
#include "NFRUViewExtension.h"
#include "NGSettings.h"
#include "NGSharedBackend.h"
#include "NGVulkanBackend/Public/NGConvertVelocity.h"
#include "NGVulkanBackend/Public/NGConvertVelocityAndDepth.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/TemporalAA.h"

#include "Async/Async.h"
#include "RenderTargetPool.h"
#include "ScenePrivate.h"
#if UE_VERSION_AT_LEAST(5, 2, 0)
#include "DataDrivenShaderPlatformInfo.h"
#endif
#include "Engine/GameViewportClient.h"
#include "Engine/RendererSettings.h"
#include "NFRUStats.h"
#include "PixelFormat.h"
#include "PixelShaderUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealClient.h"

#if UE_VERSION_AT_LEAST(5, 0, 0)
#define GET_RHI_TARGET_ARG
#define GET_RHI_VIEW_ARG
#define GET_TEXTURE
#else
#define GET_RHI_TARGET_ARG ERenderTargetTexture::Targetable
#define GET_RHI_VIEW_ARG ERenderTargetTexture::ShaderResource
#define GET_TEXTURE .GetTexture()
#endif

static FIntPoint CalculateNFRUMVDepthLane(FIntPoint RenderSize, FIntPoint DisplaySize)
{
	if (RenderSize.X <= 0 || RenderSize.Y <= 0 || DisplaySize.X <= 0 || DisplaySize.Y <= 0)
	{
		return RenderSize;
	}

	const FfxNfruResolutionPlan ResolutionPlan =
		ffxNfruDeriveResolutionPlan({static_cast<uint32>(DisplaySize.X), static_cast<uint32>(DisplaySize.Y)},
			{static_cast<uint32>(RenderSize.X), static_cast<uint32>(RenderSize.Y)},
			FFX_FRAMEINTERPOLATION_DATA_GRAPH_ALIGNMENT);

	// Preserve native inputs unless the SDK would select an all-input downsample pass.
	if (!ResolutionPlan.needsAllInputDownsample)
	{
		return RenderSize;
	}

	return FIntPoint(
		static_cast<int32>(ResolutionPlan.mvDepthLane.width), static_cast<int32>(ResolutionPlan.mvDepthLane.height));
}

#if PLATFORM_WINDOWS
static bool IsBackBufferFormatSupportedByNfruOpticalFlow(EPixelFormat UEFormat)
{
	switch (UEFormat)
	{
		case PF_R8G8B8A8:
		case PF_B8G8R8A8:
		case PF_FloatR11G11B10:
		case PF_R8:
			return true;
		default:
			return false;
	}
}

static void ReportUnsupportedNfruBackBufferFormat(EPixelFormat UEFormat)
{
	static bool s_hasShownUnsupportedFormatPopup = false;
	if (s_hasShownUnsupportedFormatPopup)
	{
		return;
	}

	// clang-format off
	const TCHAR* RawName = GetPixelFormatString(UEFormat);
	const FString FormatName = (RawName && RawName[0] != TEXT('\0')) ?
		FString(RawName) : FString::Printf(TEXT("UnknownEPixelFormat(%d)"), (int32)UEFormat);

	const FString Msg = FString::Printf(
		TEXT("NFRU disabled for unsupported backbuffer format: %s.\n\n"
			 "Supported formats: PF_R8G8B8A8, PF_B8G8R8A8, PF_FloatR11G11B10, PF_R8."),
		*FormatName);
	// clang-format on

	AsyncTask(ENamedThreads::GameThread,
		[Msg]() { FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *Msg, TEXT("Arm NG NFRU")); });
	s_hasShownUnsupportedFormatPopup = true;
}
#endif

extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

CSV_DEFINE_CATEGORY(FrameGen, true);

//------------------------------------------------------------------------------------------------------
// Input declaration for the frame interpolation pass.
//------------------------------------------------------------------------------------------------------
struct NFRUPass
{
	BEGIN_SHADER_PARAMETER_STRUCT(FComputeParameters, )
	RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(ColorBufferTm1, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(SceneDepth, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(SceneDepthTm1, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(MotionVectors, ERHIAccess::SRVCompute)
	RDG_TEXTURE_ACCESS(HudTexture, ERHIAccess::CopyDest)
	RDG_TEXTURE_ACCESS(InterpolatedRT, ERHIAccess::UAVCompute)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FFragmentParameters, )
	RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(ColorBufferTm1, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(SceneDepth, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(SceneDepthTm1, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(MotionVectors, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(HudTexture, ERHIAccess::CopyDest)
	RDG_TEXTURE_ACCESS(InterpolatedRT, ERHIAccess::RTV)
	RDG_TEXTURE_ACCESS(BackBufferTexture, ERHIAccess::RTV)
	END_SHADER_PARAMETER_STRUCT()
};

#if UE_VERSION_OLDER_THAN(5, 1, 0)
inline void TransitionAndCopyTexture(
	FRHICommandList& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FRHICopyTextureInfo& Info)
{
	check(SrcTexture && DstTexture);
	check(SrcTexture->GetNumSamples() == DstTexture->GetNumSamples());

	if (SrcTexture == DstTexture)
	{
		RHICmdList.Transition({FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask)});
		return;
	}

	RHICmdList.Transition({FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc),
		FRHITransitionInfo(DstTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest)});

	RHICmdList.CopyTexture(SrcTexture, DstTexture, Info);

	RHICmdList.Transition({FRHITransitionInfo(SrcTexture, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
		FRHITransitionInfo(DstTexture, ERHIAccess::CopyDest, ERHIAccess::SRVMask)});
}
#endif

static FRDGTextureRef CopyAndCropTextureIfNeeded(
	FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const FIntRect& SourceRect, const TCHAR* NameIfNeeded)
{
	const FIntRect TextureRect(FIntPoint::ZeroValue, Texture->Desc.Extent);
	if (SourceRect == TextureRect)
	{
		return Texture;
	}

	check(SourceRect.Min.X >= 0 && SourceRect.Min.Y >= 0);
	check(SourceRect.Max.X <= Texture->Desc.Extent.X && SourceRect.Max.Y <= Texture->Desc.Extent.Y);

	FRDGTextureDesc Desc = Texture->Desc;
	Desc.Extent = SourceRect.Size();
	FRDGTextureRef CroppedTexture = GraphBuilder.CreateTexture(Desc, NameIfNeeded, ERDGTextureFlags::MultiFrame);

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.SourcePosition = FIntVector(SourceRect.Min.X, SourceRect.Min.Y, 0);
	CopyInfo.Size = FIntVector(SourceRect.Width(), SourceRect.Height(), 1);
	AddCopyTexturePass(GraphBuilder, Texture, CroppedTexture, CopyInfo);
	return CroppedTexture;
}

void NFRUMsgCallback(uint32_t type, const char* message)
{
	switch (type)
	{
		case FFX_API_MESSAGE_TYPE_ERROR:
			UE_LOG(LogNFRU, Error, TEXT("NFRU Error: %s"), UTF8_TO_TCHAR(message));
			break;
		case FFX_API_MESSAGE_TYPE_WARNING:
			UE_LOG(LogNFRU, Warning, TEXT("NFRU Warning: %s"), UTF8_TO_TCHAR(message));
			break;
		default:
			UE_LOG(LogNFRU, Log, TEXT("NFRU Message: %s"), UTF8_TO_TCHAR(message));
			break;
	}
}

//------------------------------------------------------------------------------------------------------
// Implementation for the Frame Interpolation.
//------------------------------------------------------------------------------------------------------
NFRU::NFRU()
	: GameDeltaTime(0.0)
	, LastTime(FPlatformTime::Seconds())
	, AverageTime(0.f)
	, AverageFPS(0.f)
	, InterpolationCount(0llu)
	, PresentCount(0llu)
	, Index(0u)
	, ResetState(0u)
	, bInterpolatedFrame(false)
{
	UGameViewportClient::OnViewportCreated().AddRaw(this, &NFRU::OnViewportCreatedHandler_SetCustomPresent);
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &NFRU::OnPostEngineInit);
	RegisterCVarCallbacks();
}

NFRU::~NFRU()
{
	UGameViewportClient::OnViewportCreated().RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FCoreDelegates::OnBeginFrame.RemoveAll(this);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->OnBeginDraw().RemoveAll(this);
		GEngine->GetPostRenderDelegateEx().RemoveAll(this);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateRenderer* SlateRenderer = FSlateApplication::Get().GetRenderer();
		if (SlateRenderer != nullptr)
		{
			SlateRenderer->OnSlateWindowRendered().RemoveAll(this);
			SlateRenderer->OnPreResizeWindowBackBuffer().RemoveAll(this);
			SlateRenderer->OnPostResizeWindowBackBuffer().RemoveAll(this);
			SlateRenderer->OnBackBufferReadyToPresent().RemoveAll(this);
		}
	}
	for (auto& Window : Windows)
	{
		if (Window.Value != nullptr)
		{
			Window.Value->SetCustomPresent(nullptr);
		}
	}
	SwapChains.Empty();
	Windows.Empty();
	ResizingWindows.Empty();
	ViewExtension = nullptr;
}

INFRUCustomPresent* NFRU::CreateCustomPresent(INGSharedBackend* Backend,
	uint32_t Flags,
	FIntPoint RenderSize,
	FIntPoint DisplaySize,
	FfxSwapchain RawSwapChain,
	FfxCommandQueue Queue,
	FfxApiSurfaceFormat Format,
	EFFXBackendAPI Api)
{
	NFRUCustomPresent* Result = new NFRUCustomPresent;
	if (Result)
	{
		if (Result->InitSwapChain(Backend, Flags, RenderSize, DisplaySize, RawSwapChain, Queue, Format, Api))
		{
			SwapChains.Add(RawSwapChain, Result);
		}
	}
	return Result;
}

bool NFRU::GetAverageFrameTimes(float& AvgTimeMs, float& AvgFPS)
{
	bool bOK = false;
	AvgTimeMs = GAverageMS;
	AvgFPS = GAverageFPS;
	auto* Engine = GEngine;
	auto GameViewport = Engine ? Engine->GameViewport : nullptr;
	auto Viewport = GameViewport ? GameViewport->Viewport : nullptr;
	auto ViewportRHI = Viewport ? Viewport->GetViewportRHI() : nullptr;
	NFRUCustomPresent* Presenter =
		ViewportRHI.IsValid() ? (NFRUCustomPresent*)ViewportRHI->GetCustomPresent() : nullptr;
	if (Presenter)
	{
		AvgTimeMs = AverageTime;
		AvgFPS = AverageFPS;
		bOK = true;
	}
	return bOK;
}

void NFRU::OnViewportCreatedHandler_SetCustomPresent()
{
	if (GEngine && GEngine->GameViewport)
	{
		if (!GEngine->GameViewport->Viewport->GetViewportRHI().IsValid()
			|| (GEngine->GameViewport->Viewport->GetViewportRHI()->GetCustomPresent() == nullptr))
		{
			GEngine->GameViewport->OnBeginDraw().AddRaw(this, &NFRU::OnBeginDrawHandler);
		}
	}
}

void NFRU::ResetFPS(IConsoleVariable* CVar)
{
	bool bShouldReset = false;

	// Check which CVar triggered the callback
	if (CVar == CVarEnableNFRU.AsVariable())
	{
		// Reset when NFRU is disabled
		bShouldReset = !CVarEnableNFRU.GetValueOnAnyThread();
	}
	else if (CVar == CVarNFRUPaceAdjuster.AsVariable())
	{
		// Reset when PaceAdjuster is disabled (only if NFRU is enabled)
		if (CVarEnableNFRU.GetValueOnAnyThread())
		{
			bShouldReset = !CVarNFRUPaceAdjuster.GetValueOnAnyThread();
		}
	}

	// Restore original FPS if reset is needed
	if (bShouldReset)
	{
		FString Cmd = FString::Printf(TEXT("r.SetFramePace %d"), DefaultFPS);
		IConsoleManager::Get().ProcessUserConsoleInput(*Cmd, *GLog, nullptr);
		LastFPS = DefaultFPS;
		bResetFPS = true;
	}
}

void NFRU::RegisterCVarCallbacks()
{
	FConsoleVariableDelegate FramePaceDelegate = FConsoleVariableDelegate::CreateRaw(this, &NFRU::ResetFPS);
	CVarEnableNFRU->SetOnChangedCallback(FramePaceDelegate);
	CVarNFRUPaceAdjuster->SetOnChangedCallback(FramePaceDelegate);
}

void NFRU::OnBeginDrawHandler()
{
	if (ShouldUseCustomFramePacing()
		&& GEngine->GameViewport->Viewport->GetViewportRHI()->GetCustomPresent() != nullptr)
	{
		NFRUCustomPresent* Presenter =
			(NFRUCustomPresent*)GEngine->GameViewport->Viewport->GetViewportRHI()->GetCustomPresent();
		if (bResetFPS)
		{
			Presenter->SetCustomFPS(DefaultFPS);
			bResetFPS = false;
		}
		int32 CustomFPS = Presenter->GetCustomFPS();

		// LastFPS hold the last set FPS to avoid setting it every frame.
		if (LastFPS != CustomFPS)
		{
			FString Cmd = FString::Printf(TEXT("r.SetFramePace %d"), CustomFPS);
			IConsoleManager::Get().ProcessUserConsoleInput(*Cmd, *GLog, nullptr);
			LastFPS = CustomFPS;
		}
	}

	if (GEngine->GameViewport->Viewport->GetViewportRHI().IsValid()
		&& (GEngine->GameViewport->Viewport->GetViewportRHI()->GetCustomPresent() == nullptr))
	{
		auto ViewportRHI = GEngine->GameViewport->Viewport->GetViewportRHI();
		void* NativeSwapChain = ViewportRHI->GetNativeSwapChain();
		NFRUCustomPresent** PresentHandler = SwapChains.Find(NativeSwapChain);
		if (PresentHandler)
		{
			(*PresentHandler)
				->InitViewport(GEngine->GameViewport->Viewport, GEngine->GameViewport->Viewport->GetViewportRHI());
		}
		else
		{
			INGSharedBackendModule* VulkanBackendModule =
				FModuleManager::GetModulePtr<INGSharedBackendModule>(TEXT("NGVulkanBackend"));
			check(VulkanBackendModule);

			auto* VulkanBackend = VulkanBackendModule->GetBackend();

			static const auto CVarDefaultBackBufferPixelFormat =
				IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));

			auto Engine = GEngine;
			auto GameViewport = Engine->GameViewport;
			auto Viewport = GameViewport->Viewport;

			if (Viewport->GetViewportRHI().IsValid() && (Viewport->GetViewportRHI()->GetCustomPresent() == nullptr))
			{
				uint32 Flags = 0;
				Flags |= bool(ERHIZBuffer::IsInverted) ? FFX_FG_CONTEXT_FLAG_ENABLE_DEPTH_INVERTED : 0;
				Flags |= FFX_FG_CONTEXT_FLAG_ENABLE_DEPTH_INFINITE;

				EPixelFormat SurfaceFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(
					EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnAnyThread()));
				SurfaceFormat = RHIPreferredPixelFormatHint(SurfaceFormat);
				auto SwapChainSize = Viewport->GetSizeXY();
				ENQUEUE_RENDER_COMMAND(NFRUCreateCustomPresent)
				(
					[this, VulkanBackend, Flags, SwapChainSize, SurfaceFormat](FRHICommandListImmediate& RHICmdList)
					{
						auto* CustomPresent = CreateCustomPresent(VulkanBackend,
							Flags,
							SwapChainSize,
							SwapChainSize,
							(FfxSwapchain) nullptr,
							(FfxCommandQueue)GDynamicRHI,
							GetFFXApiFormat(SurfaceFormat, false),
							EFFXBackendAPI::Vulkan);
						if (CustomPresent)
						{
							CustomPresent->InitViewport(
								GEngine->GameViewport->Viewport, GEngine->GameViewport->Viewport->GetViewportRHI());
						}
					});
			}
		}
	}
}

void NFRU::CalculateFPSTimings()
{
	auto* Engine = GEngine;
	auto GameViewport = Engine ? Engine->GameViewport : nullptr;
	auto Viewport = GameViewport ? GameViewport->Viewport : nullptr;
	auto ViewportRHI = Viewport ? Viewport->GetViewportRHI() : nullptr;
	NFRUCustomPresent* Presenter =
		ViewportRHI.IsValid() ? (NFRUCustomPresent*)ViewportRHI->GetCustomPresent() : nullptr;
	if (CVarEnableNFRU.GetValueOnAnyThread() != 0 && Presenter && Presenter->GetMode() == ENFRUPresentModeRHI)
	{
		double CurrentTime = FPlatformTime::Seconds();
		float FrameTimeMS = (float)((CurrentTime - LastTime) * 1000.0);
		AverageTime = AverageTime * 0.75f + FrameTimeMS * 0.25f;
		LastTime = CurrentTime;
		AverageFPS = 1000.f / AverageTime;

		if (CVarNFRUUpdateGlobalFrameTime.GetValueOnAnyThread() != 0)
		{
			GAverageMS = AverageTime;
			GAverageFPS = AverageFPS;
		}

		SET_FLOAT_STAT(STAT_FrameGen_RenderFPS, AverageFPS);
		SET_FLOAT_STAT(STAT_FrameGen_RenderInterval, AverageTime);
		CSV_CUSTOM_STAT(FrameGen, RenderFPS, AverageFPS, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FrameGen, RenderInterval, AverageTime, ECsvCustomStatOp::Set);

		float PacedFPS;
		float PacedMs;
		Presenter->GetAverageFrameTimes(PacedMs, PacedFPS);

		SET_FLOAT_STAT(STAT_FrameGen_PresentFPS, PacedFPS);
		SET_FLOAT_STAT(STAT_FrameGen_PresentInterval, PacedMs);
		CSV_CUSTOM_STAT(FrameGen, PresentFPS, PacedFPS, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(FrameGen, PresentInterval, PacedMs, ECsvCustomStatOp::Set);
	}
}

void NFRU::OnPostEngineInit()
{
	EFFXBackendAPI Api;
	if (!INGSharedBackend::GetApiAccessor(Api))
	{
		UE_LOG(LogNFRU, Warning, TEXT("NFRU is disabled because Vulkan RHI or ngSDK is not available at runtime."));
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();

		// Has to be used by all backends as otherwise we end up waiting on DrawBuffers.
		{
			FSlateApplicationBase& BaseApp = static_cast<FSlateApplicationBase&>(App);
			FFXSlateApplication& Accessor = (FFXSlateApplication&)BaseApp;
			TSharedPtr<FSlateRenderer>* Ptr = Accessor.GetRendererRef();
			auto SharedRef = Ptr->ToSharedRef();
			TSharedRef<NFRUSlateRenderer> RendererWrapper = MakeShared<NFRUSlateRenderer>(SharedRef);
			App.InitializeRenderer(RendererWrapper, true);
		}

		{
			FSlateRenderer* SlateRenderer = App.GetRenderer();
			SlateRenderer->OnSlateWindowRendered().AddRaw(const_cast<NFRU*>(this), &NFRU::OnSlateWindowRendered);
			SlateRenderer->OnPreResizeWindowBackBuffer().AddRaw(this, &NFRU::OnPreResizeWindowBackBufferCallback);
			SlateRenderer->OnPostResizeWindowBackBuffer().AddRaw(this, &NFRU::OnPostResizeWindowBackBufferCallback);
			SlateRenderer->OnBackBufferReadyToPresent().AddRaw(
				const_cast<NFRU*>(this), &NFRU::OnBackBufferReadyToPresentCallback);
			GEngine->GetPostRenderDelegateEx().AddRaw(const_cast<NFRU*>(this), &NFRU::InterpolateFrame);

			NFRU* Self = this;
			FCoreDelegates::OnBeginFrame.AddLambda(
				[Self]()
				{
					ENQUEUE_RENDER_COMMAND(BeginFrameRT)
					([Self](FRHICommandListImmediate& RHICmdList) { /*Self->CalculateFPSTimings();*/ });
				});

			ViewExtension = FSceneViewExtensions::NewExtension<NFRUViewExtension>(this);
		}
	}
}
template <typename TInputs> void NFRU::SetupView(const FSceneView& InView, const TInputs& Inputs)
{
	if (InView.bIsViewInfo)
	{
		NFRUView View;
		View.ViewFamilyTexture = Inputs.ViewFamilyTexture;
#if UE_VERSION_AT_LEAST(5, 0, 0)
		View.SceneDepth = Inputs.SceneTextures->GetContents()->SceneDepthTexture;
		if constexpr (std::is_same_v<TInputs, FMobilePostProcessingInputs>)
		{
			View.SceneVelocity = Inputs.SceneTextures->GetContents()->SceneVelocityTexture;
		}
		else
		{
			View.SceneVelocity = Inputs.SceneTextures->GetContents()->GBufferVelocityTexture;
		}
		View.CameraNear = InView.ViewMatrices.ComputeNearPlane();
#else
		View.SceneDepth = (*Inputs.SceneTextures)->SceneDepthTexture;
		if constexpr (std::is_same_v<TInputs, FMobilePostProcessingInputs>)
		{
			View.SceneVelocity = (*Inputs.SceneTextures)->SceneVelocityTexture;
		}
		else
		{
			View.SceneVelocity = (*Inputs.SceneTextures)->GBufferVelocityTexture;
		}
		View.CameraNear = GNearClippingPlane;
#endif
		View.ViewRect = ((FViewInfo const&)InView).ViewRect;
		View.InputExtentsQuantized = View.ViewRect.Size();
		QuantizeSceneBufferSize(((FViewInfo const&)InView).GetSecondaryViewRectSize(), View.OutputExtents);
		View.OutputExtents = FIntPoint(FMath::Max(View.InputExtentsQuantized.X, View.OutputExtents.X),
			FMath::Max(View.InputExtentsQuantized.Y, View.OutputExtents.Y));
		View.bReset = InView.bCameraCut;
		View.CameraFOV = InView.ViewMatrices.ComputeHalfFieldOfViewPerAxis().Y * 2.0f;
#if UE_VERSION_AT_LEAST(5, 1, 0)
		View.bEnabled = InView.bIsGameView && !InView.bIsSceneCapture && !InView.bIsSceneCaptureCube
						&& !InView.bIsReflectionCapture && !InView.bIsPlanarReflection;
#else
		View.bEnabled = InView.bIsGameView && !InView.bIsSceneCapture && !InView.bIsReflectionCapture
						&& !InView.bIsPlanarReflection;
#endif
		View.TemporalJitterPixels = ((FViewInfo const&)InView).TemporalJitterPixels;
#if UE_VERSION_AT_LEAST(5, 0, 0)
		if (View.bEnabled)
		{
			View.GameTimeMs = InView.Family->Time.GetDeltaWorldTimeSeconds();
			GameDeltaTime = InView.Family->Time.GetDeltaWorldTimeSeconds();
#else
		if (View.bEnabled)
		{
			GameDeltaTime = InView.Family->DeltaWorldTime;
#endif
			Views.Add(&InView, View);
		}
	}
}

// Explicit template instantiations
template void NFRU::SetupView(const FSceneView&, const FPostProcessingInputs&);
template void NFRU::SetupView(const FSceneView&, const FMobilePostProcessingInputs&);

#if UE_VERSION_OLDER_THAN(5, 1, 0)
enum class EDisplayOutputFormat : uint8
{
	SDR_sRGB,
	SDR_Rec709,
	SDR_ExplicitGammaMapping,
	HDR_ACES_1000nit_ST2084,
	HDR_ACES_2000nit_ST2084,
	HDR_ACES_1000nit_ScRGB,
	HDR_ACES_2000nit_ScRGB,
};
#endif

bool NFRU::InterpolateView(FRDGBuilder& GraphBuilder,
	NFRUCustomPresent* Presenter,
	const FSceneView* View,
	NFRUView const& ViewDesc,
	FRDGTextureRef FinalBuffer,
	FRDGTextureRef ColorBufferTm1,
	FRDGTextureRef InterpolatedRDG,
	FRDGTextureRef BackBufferRDG,
	uint32 InterpolateIndex)
{
	bool bInterpolated = false;
	auto* Engine = GEngine;
	auto GameViewport = Engine ? Engine->GameViewport : nullptr;
	auto Viewport = GameViewport ? GameViewport->Viewport : nullptr;
	auto ViewportRHI = Viewport ? Viewport->GetViewportRHI() : nullptr;
	FIntPoint ViewportSizeXY = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;

	FRDGTextureRef ViewFamilyTexture = ViewDesc.ViewFamilyTexture;
	FIntRect ViewRect = ViewDesc.ViewRect;
	FIntPoint InputExtents = ViewDesc.ViewRect.Size();

	FIntPoint InputExtentsQuantized = ViewDesc.InputExtentsQuantized;
	FIntPoint OutputExtents = ((FViewInfo*)View)->UnscaledViewRect.Size();
	FIntPoint OutputPoint = ((FViewInfo*)View)->UnscaledViewRect.Min;
	float CameraNear = ViewDesc.CameraNear;
	float CameraFOV = ViewDesc.CameraFOV;
	bool bEnabled = ViewDesc.bEnabled;
	bool bReset = ViewDesc.bReset || (ResetState == 0);
	bool const bResized = Presenter->Resized();
	float DeltaTimeMs = GameDeltaTime * 1000.f;
	FRHICopyTextureInfo Info;

	ffxApiDispatchDescFrameGenerationPrepare UpscalerDesc;
	FMemory::Memzero(UpscalerDesc);
	UpscalerDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
#if UE_VERSION_AT_LEAST(5, 3, 0)
	UpscalerDesc.frameID = View->Family->FrameCounter;
#else
	UpscalerDesc.frameID = GFrameCounterRenderThread;
#endif
#if UE_VERSION_AT_LEAST(5, 0, 0)
	UpscalerDesc.frameTimeDelta = View->Family->Time.GetDeltaWorldTimeSeconds() * 1000.f;
#else
	UpscalerDesc.frameTimeDelta = DeltaTimeMs;
#endif
	if (bool(ERHIZBuffer::IsInverted))
	{
		UpscalerDesc.cameraNear = FLT_MAX;
		UpscalerDesc.cameraFar = CameraNear;
	}
	else
	{
		UpscalerDesc.cameraNear = CameraNear;
		UpscalerDesc.cameraFar = FLT_MAX;
	}
	UpscalerDesc.cameraFovAngleVertical = CameraFOV;
	UpscalerDesc.viewSpaceToMetersFactor = 1.f / View->WorldToMetersScale;

	UpscalerDesc.jitterOffset.x = ((FViewInfo*)View)->TemporalJitterPixels.X;
	UpscalerDesc.jitterOffset.y = ((FViewInfo*)View)->TemporalJitterPixels.Y;

	const auto ViewProj = View->ViewMatrices.GetViewMatrix() * View->ViewMatrices.GetProjectionNoAAMatrix();
	const auto vpMatrix = ViewProj.M;

	// It seems like the Unreal ViewProj matrix is in column-major by default, so flatten and cast without transposing.
	for (int i = 0; i < 16; i++)
	{
		UpscalerDesc.viewProjection[i] = static_cast<float>(vpMatrix[i / 4][i % 4]);
	}

	// Get the primary view size before upscaling as NFRU works with inputs before upscaling, as it's assumed NSS is
	// also in the frame.
	FIntPoint RenderSize = ((FViewInfo*)View)->ViewRect.Size();

	const FIntPoint DepthLaneExtent = CalculateNFRUMVDepthLane(RenderSize, OutputExtents);
	const bool bLaneAspectMatchesRender =
		static_cast<int64>(DepthLaneExtent.X) * RenderSize.Y == static_cast<int64>(RenderSize.X) * DepthLaneExtent.Y;
	const bool bSDKManagesDepth = CVarEnableNSS.GetValueOnAnyThread() != 0 || !bLaneAspectMatchesRender;
	const FIntPoint DepthExtent = bSDKManagesDepth ? InputExtents : DepthLaneExtent;
	UpscalerDesc.motionVectorScale.x = static_cast<float>(DepthExtent.X);
	UpscalerDesc.motionVectorScale.y = static_cast<float>(DepthExtent.Y);

	ffxApiCreateContextDescFrameGeneration FgDesc;
	FMemory::Memzero(FgDesc);
	FgDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	const EPixelFormat BackBufferUEFormat = BackBufferRDG->Desc.Format;
#if PLATFORM_WINDOWS
	if (!IsBackBufferFormatSupportedByNfruOpticalFlow(BackBufferUEFormat))
	{
		ReportUnsupportedNfruBackBufferFormat(BackBufferUEFormat);
		Presenter->SetEnabled(false);
		return false;
	}
#endif
	FgDesc.backBufferFormat = GetFFXApiFormat(BackBufferUEFormat, false);
	FgDesc.displaySize.width = (uint32)OutputExtents.X;
	FgDesc.displaySize.height = (uint32)OutputExtents.Y;
	FgDesc.renderSize.width = RenderSize.X;
	FgDesc.renderSize.height = RenderSize.Y;
	FgDesc.mvDepthSize.width = static_cast<uint32>(DepthExtent.X);
	FgDesc.mvDepthSize.height = static_cast<uint32>(DepthExtent.Y);
	FgDesc.fpMessage = NFRUMsgCallback;
	FgDesc.flags |= (bool(ERHIZBuffer::IsInverted)) ? FFX_API_FG_CONTEXT_FLAG_ENABLE_DEPTH_INVERTED : 0;
	FgDesc.flags |= FFX_API_FG_CONTEXT_FLAG_ENABLE_HIGH_DYNAMIC_RANGE | FFX_API_FG_CONTEXT_FLAG_ENABLE_DEPTH_INFINITE;
	if (bSDKManagesDepth)
	{
		// NSS/NFRU mode uses SDK-owned depth history, even when this frame has no
		// NSS MV cache and must fall back to the NFRU lane MV conversion.
		FgDesc.flags |= FFX_API_FG_CONTEXT_FLAG_MANAGE_PREVIOUS_DEPTH;
	}

	const bool bUseFragmentShader = CVarNFRUUseFragmentShader.GetValueOnAnyThread() != 0;
	// Fragment stages are the SDK default; select compute explicitly.
	if (!bUseFragmentShader)
	{
		FgDesc.flags |= FFX_API_FG_CONTEXT_FLAG_ALL_STAGES_COMPUTE;
		FgDesc.flags |= FFX_API_FG_CONTEXT_FLAG_MV_HINTS_COMPUTE;
	}

	// At context creation there is no previous frame yet, so this field seeds the matrix of the frame preceding the
	// first interpolation; pass the same matrix you will use for the first rendered frame.
	for (int i = 0; i < 16; i++)
	{
		FgDesc.initialViewProjection[i] = static_cast<float>(vpMatrix[i / 4][i % 4]);
	}
	FRDGTextureRef ColorBuffer = FinalBuffer;
	FRDGTextureRef InterBuffer = InterpolatedRDG;
	FRDGTextureRef HudBuffer = nullptr;
	FFXFIResourceRef Context =
		Presenter->UpdateContexts(GraphBuilder, ((FSceneViewState*)View->State)->UniqueID, FgDesc);

	if (!Context.IsValid())
	{
		UE_LOG(LogNFRU, Warning, TEXT("NFRU is disabled because invalid context."));
		Presenter->SetEnabled(false);
		return false;
	}

	//------------------------------------------------------------------------------------------------------
	// Consolidate Motion Vectors
	//   UE motion vectors are in sparse format by default. Convert them to a format consumable by FFX.
	//------------------------------------------------------------------------------------------------------
	FRDGTextureRef ConvertedDepthTexture = nullptr;
	const EPixelFormat DepthFormat = PF_R32_FLOAT;

	FRDGTextureRef DepthTm1Texture = ViewDesc.SceneDepth;
	FRDGTextureRef DepthWriteTexture = nullptr;
	uint32 DepthWriteIndex = 0;
	if (!bSDKManagesDepth)
	{
		for (uint32 DepthIndex = 0; DepthIndex < UE_ARRAY_COUNT(Context->DepthTM1RT); ++DepthIndex)
		{
			if (!IsValidRef(Context->DepthTM1RT[DepthIndex])
				|| Context->DepthTM1RT[DepthIndex]->GetDesc().Extent != DepthExtent
				|| Context->DepthTM1RT[DepthIndex]->GetDesc().Format != DepthFormat)
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(DepthExtent,
					DepthFormat,
					FClearValueBinding::Transparent,
					TexCreate_ShaderResource,
					TexCreate_ShaderResource | TexCreate_RenderTargetable,
					false));
				GRenderTargetPool.FindFreeElement(
					GraphBuilder.RHICmdList, Desc, Context->DepthTM1RT[DepthIndex], TEXT("FFXFIDepthTM1PingPong"));
			}
		}

		if (!IsValidRef(Context->DepthTM1RT[0]) || !IsValidRef(Context->DepthTM1RT[1]))
		{
			UE_LOG(LogNFRU, Warning, TEXT("NFRU disabled because Depth history resources could not be created."));
			return false;
		}

		const uint32 DepthHistoryIndex = Context->DepthHistoryIndex & 1u;
		DepthWriteIndex = 1u - DepthHistoryIndex;
		DepthTm1Texture = GraphBuilder.RegisterExternalTexture(Context->DepthTM1RT[DepthHistoryIndex]);
		DepthWriteTexture = GraphBuilder.RegisterExternalTexture(Context->DepthTM1RT[DepthWriteIndex]);

		if (bReset)
		{
			AddClearRenderTargetPass(GraphBuilder, DepthTm1Texture, FLinearColor::Black);
			AddClearRenderTargetPass(GraphBuilder, DepthWriteTexture, FLinearColor::Black);
		}
	}

	const FIntRect ConvertViewRect = ViewDesc.ViewRect;
	const FNGCachedNSSInputs CachedNSSInputs =
		helper_NGGetCachedNSSInputs(GraphBuilder, ConvertViewRect, ((FViewInfo*)View));
	// NSS has already copied these inputs into zero-origin view-sized textures.
	// Reuse them directly; NFRU must not crop or apply the viewport offset again.
	const bool bHasNSSInputs = CachedNSSInputs.Velocity != nullptr && CachedNSSInputs.Depth != nullptr
							   && CachedNSSInputs.Depth->Desc.Extent == CachedNSSInputs.Velocity->Desc.Extent
							   && CachedNSSInputs.Depth->Desc.Extent != FIntPoint::ZeroValue;
	FRDGTextureRef MotionVectorTexture = bHasNSSInputs ? CachedNSSInputs.Velocity : nullptr;
	FRDGTextureRef DepthInputTexture = bHasNSSInputs ? CachedNSSInputs.Depth : nullptr;

	if (!bInitVelocityCacheState || bLastVelocityCacheState != bHasNSSInputs)
	{
		UE_LOG(LogNFRU,
			Warning,
			TEXT("NSS velocity cache state changed: %s."),
			bHasNSSInputs ? TEXT("hit; using NSS full-resolution MV") : TEXT("miss; using NFRU MV/depth conversion"));
		bLastVelocityCacheState = bHasNSSInputs;
		bInitVelocityCacheState = true;
	}

	if (!bHasNSSInputs)
	{
		const FNGConvertVelocityResult Conversion = helper_NGConvertVelocityAndDepth(GraphBuilder,
			ViewDesc.SceneDepth,
			ViewDesc.SceneVelocity,
			ConvertViewRect,
			((FViewInfo*)View),
			!bUseFragmentShader,
			DepthExtent,
			bSDKManagesDepth ? nullptr : DepthWriteTexture);
		if (!Conversion.Velocity || (!bSDKManagesDepth && !Conversion.Depth))
		{
			UE_LOG(LogNFRU, Warning, TEXT("NFRU disabled because MV/Depth lane conversion failed."));
			return false;
		}
		MotionVectorTexture = Conversion.Velocity;
		DepthInputTexture = Conversion.Depth;
		;
	}
	if (!MotionVectorTexture || !DepthInputTexture)
	{
		UE_LOG(LogNFRU, Warning, TEXT("NFRU disabled because MV/depth input conversion failed."));
		return false;
	}

	if (Context->Desc.displaySize.width != ViewportSizeXY.X || Context->Desc.displaySize.height != ViewportSizeXY.Y)
	{
		if (!IsValidRef(Context->Color) || Context->Color->GetDesc().Extent.X != Context->Desc.displaySize.width
			|| Context->Color->GetDesc().Extent.Y != Context->Desc.displaySize.height
			|| Context->Color->GetDesc().Format != BackBufferRDG->Desc.Format)
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(Context->Desc.displaySize.width, Context->Desc.displaySize.height),
				BackBufferRDG->Desc.Format,
				FClearValueBinding::Transparent,
				TexCreate_ShaderResource,
				bUseFragmentShader ? TexCreate_RenderTargetable : TexCreate_UAV,
				false,
				1,
				true,
				true));
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Context->Color, TEXT("FIColor"));
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Context->Inter, TEXT("FIInter"));
		}

		FRHICopyTextureInfo CopyInfo;
		ColorBuffer = GraphBuilder.RegisterExternalTexture(Context->Color);
		CopyInfo.SourcePosition.X = OutputPoint.X;
		CopyInfo.SourcePosition.Y = OutputPoint.Y;
		CopyInfo.Size.X = FMath::Min((uint32)Context->Desc.displaySize.width, (uint32)FinalBuffer->Desc.Extent.X);
		CopyInfo.Size.Y = FMath::Min((uint32)Context->Desc.displaySize.height, (uint32)FinalBuffer->Desc.Extent.Y);
		AddCopyTexturePass(GraphBuilder, FinalBuffer, ColorBuffer, CopyInfo);

		// InterpolatedRDG is a full-window texture, but the frame-generation result only fills
		// OutputRect. Initialize it from the real no-UI frame before the partial copy below so
		// the full-window InterpolatedNoUI capture does not treat the viewport surroundings as UI.
		const FIntRect OutputRect(OutputPoint, OutputPoint + OutputExtents);
		const FIntRect InterpolatedRect(FIntPoint::ZeroValue, InterpolatedRDG->Desc.Extent);
		const bool bHasPartialInterpolatedOutput = OutputRect != InterpolatedRect;
		if (bHasPartialInterpolatedOutput)
		{
			ColorBufferTm1 =
				CopyAndCropTextureIfNeeded(GraphBuilder, ColorBufferTm1, OutputRect, TEXT("FIPreviousColorCropped"));
			InterBuffer = GraphBuilder.RegisterExternalTexture(Context->Inter);
			if (CVarNFRUCaptureDebugUI.GetValueOnAnyThread())
			{
				// Keep the no-UI texture outside OutputRect consistent with the real frame.
				// The generated result overwrites OutputRect in the copy below.
				const FRHICopyTextureInfo FullCopyInfo;
				AddCopyTexturePass(GraphBuilder, FinalBuffer, InterpolatedRDG, FullCopyInfo);
			}
		}
	}

	// clang-format off
	const bool bWholeScreen = InterpolatedRDG == InterBuffer;
	const bool bDirectToBackBuffer =
		bWholeScreen && bUseFragmentShader && !CVarNFRUCaptureDebugUI.GetValueOnAnyThread();
	auto DoAddPass = [&](auto* PassParameters, FRDGTextureRef OutputBackBuffer) {
		PassParameters->ColorTexture = ColorBuffer;
		PassParameters->ColorBufferTm1 = ColorBufferTm1;
		PassParameters->HudTexture = HudBuffer;
		PassParameters->InterpolatedRT = bDirectToBackBuffer ? nullptr : InterBuffer;
		PassParameters->SceneDepth = DepthInputTexture;
		PassParameters->SceneDepthTm1 = DepthTm1Texture;
		PassParameters->MotionVectors = MotionVectorTexture;

		static const auto CVarHDRMinLuminanceLog10 =
			IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.HDR.Display.MinLuminanceLog10"));
		static const auto CVarHDRMaxLuminance =
			IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.MaxLuminance"));

		float GHDRMinLuminnanceLog10 = CVarHDRMinLuminanceLog10 ? CVarHDRMinLuminanceLog10->GetValueOnAnyThread() : 0.f;
		int32 GHDRMaxLuminnance = CVarHDRMaxLuminance ? CVarHDRMaxLuminance->GetValueOnAnyThread() : 1;
#if UE_VERSION_AT_LEAST(5, 1, 0)
		EDisplayOutputFormat ViewportOutputFormat = Viewport->GetDisplayOutputFormat();
#else
		static const auto CVarHDROutputDevice =
			IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));
		static const auto CVarHDROutputEnabled =
			IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.EnableHDROutput"));
		const EDisplayOutputFormat ViewportOutputFormat =
			(CVarHDROutputDevice && GRHISupportsHDROutput
			&& (CVarHDROutputEnabled && CVarHDROutputEnabled->GetValueOnAnyThread() != 0)) ?
			EDisplayOutputFormat(CVarHDROutputDevice->GetValueOnAnyThread()) :
			EDisplayOutputFormat::SDR_sRGB;
		if ((GHDRMaxLuminnance == 0)
			&& (ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ST2084
			|| ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_2000nit_ST2084
			|| ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ScRGB
			|| ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_2000nit_ScRGB))
		{
			if (ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ST2084
				|| ViewportOutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ScRGB)
			{
				GHDRMaxLuminnance = 1000;
			}
			else
			{
				GHDRMaxLuminnance = 2000;
			}
		}
#endif

		// compute how many VSync intervals interpolated and real frame should be displayed
		ffxApiDispatchDescFrameGeneration* interpolateParams = new ffxApiDispatchDescFrameGeneration;
		{
			interpolateParams->header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
			interpolateParams->header.pNext = nullptr;
			interpolateParams->numGeneratedFrames = 1;
#if UE_VERSION_AT_LEAST(5, 3, 0)
			interpolateParams->frameID = View->Family->FrameCounter;
#else
			interpolateParams->frameID = GFrameCounterRenderThread;
#endif
			interpolateParams->reset = bReset;
		}

		ffxApiConfigureDescFrameGeneration ConfigDesc;
		FMemory::Memzero(ConfigDesc);
		ConfigDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		ConfigDesc.swapChain = Presenter->GetBackend()->GetSwapchain(ViewportRHI->GetNativeSwapChain());
		ConfigDesc.frameGenerationEnabled = true;
		ConfigDesc.frameID = interpolateParams->frameID;
		ConfigDesc.flags |= FFX_API_FG_DISPATCH_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
		ConfigDesc.flags |= CVarNFRUShowDebugView.GetValueOnAnyThread() ? FFX_API_FG_DISPATCH_FLAG_DRAW_DEBUG_VIEW : 0;
#endif

		if (!bResized)
		{
			bInterpolated = true;
			const ERDGPassFlags PassFlags = bUseFragmentShader
				? ERDGPassFlags::Raster | ERDGPassFlags::NeverCull | ERDGPassFlags::SkipRenderPass
				: ERDGPassFlags::Compute | ERDGPassFlags::NeverCull;
			GraphBuilder.AddPass(RDG_EVENT_NAME("FidelityFX-FrameInterpolation"),
				PassParameters,
				PassFlags,
				[InterpolateIndex,
				UpscalerDesc,
				ConfigDesc,
				OutputExtents,
				OutputPoint,
				ViewportRHI,
				Presenter,
				Context,
				PassParameters,
				OutputBackBuffer,
				interpolateParams,
				DeltaTimeMs,
				Engine,
				ViewportOutputFormat,
				GHDRMinLuminnanceLog10,
				GHDRMaxLuminnance,
				bUseFragmentShader,
				bWholeScreen,
				bDirectToBackBuffer
			](FRHICommandListImmediate& RHICmdList)
			{
				Context->FrameSync.BeginWork(RHICmdList);
				PassParameters->ColorTexture->MarkResourceAsUsed();
				PassParameters->ColorBufferTm1->MarkResourceAsUsed();
				if (PassParameters->InterpolatedRT)
				{
					PassParameters->InterpolatedRT->MarkResourceAsUsed();
				}
				if (OutputBackBuffer)
				{
					OutputBackBuffer->MarkResourceAsUsed();
				}
				if (PassParameters->HudTexture)
				{
					PassParameters->HudTexture->MarkResourceAsUsed();
				}
				PassParameters->SceneDepth->MarkResourceAsUsed();
				PassParameters->SceneDepthTm1->MarkResourceAsUsed();
				PassParameters->MotionVectors->MarkResourceAsUsed();

				ffxApiConfigureDescFrameGeneration ConfigureDesc = ConfigDesc;

				if (InterpolateIndex != 0)
				{
					ConfigureDesc.swapChain = nullptr;
					ConfigureDesc.presentCallback = nullptr;
					ConfigureDesc.presentCallbackUserContext = nullptr;
					ConfigureDesc.frameGenerationCallback = nullptr;
					ConfigureDesc.frameGenerationCallbackUserContext = nullptr;
					ConfigureDesc.flags |= FFX_API_FG_DISPATCH_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
				}

				ffxApiDispatchDescFrameGenerationPrepare PrepareDesc = UpscalerDesc;
				const FfxApiResourceState ShaderWriteResourceState = bUseFragmentShader ?
					FFX_API_RESOURCE_STATE_RENDER_TARGET : FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
				const FfxApiResourceState ShaderReadResourceState = bUseFragmentShader ?
					FFX_API_RESOURCE_STATE_PIXEL_READ : FFX_API_RESOURCE_STATE_COMPUTE_READ;
				{
#if UE_VERSION_AT_LEAST(5, 0, 0)
					interpolateParams->presentColor = Presenter->GetBackend()->GetNativeResource(
						PassParameters->ColorTexture, ShaderReadResourceState);
#else
					interpolateParams->presentColor =
						Presenter->GetBackend()->GetNativeResource(PassParameters->ColorTexture.GetTexture(),
						ShaderReadResourceState);
#endif
				}
#if UE_VERSION_AT_LEAST(5, 0, 0)
				PrepareDesc.depth =
					Presenter->GetBackend()->GetNativeResource(PassParameters->SceneDepth, ShaderReadResourceState);
				PrepareDesc.depthTm1 = Presenter->GetBackend()->GetNativeResource(
					PassParameters->SceneDepthTm1, ShaderReadResourceState);
				PrepareDesc.colorTm1 = Presenter->GetBackend()->GetNativeResource(
					PassParameters->ColorBufferTm1, ShaderReadResourceState);
				PrepareDesc.motionVectors =
					Presenter->GetBackend()->GetNativeResource(PassParameters->MotionVectors, ShaderReadResourceState);
				auto InterpolatedRes = bDirectToBackBuffer
					? Presenter->GetBackend()->GetNativeResource(OutputBackBuffer, FFX_API_RESOURCE_STATE_RENDER_TARGET)
					: Presenter->GetBackend()->GetNativeResource(PassParameters->InterpolatedRT, ShaderWriteResourceState);
#else
				PrepareDesc.depth = Presenter->GetBackend()->GetNativeResource(
					PassParameters->SceneDepth.GetTexture(), ShaderReadResourceState);
				PrepareDesc.depthTm1 = Presenter->GetBackend()->GetNativeResource(
					PassParameters->SceneDepthTm1.GetTexture(), ShaderReadResourceState);
				PrepareDesc.colorTm1 = Presenter->GetBackend()->GetNativeResource(
					PassParameters->ColorBufferTm1.GetTexture(), ShaderReadResourceState);
				PrepareDesc.motionVectors = Presenter->GetBackend()->GetNativeResource(
					PassParameters->MotionVectors.GetTexture(), ShaderReadResourceState);
				auto InterpolatedRes = bDirectToBackBuffer
					? Presenter->GetBackend()->GetNativeResource(OutputBackBuffer, FFX_API_RESOURCE_STATE_RENDER_TARGET)
					: Presenter->GetBackend()->GetNativeResource(PassParameters->InterpolatedRT.GetTexture(),
						ShaderWriteResourceState);
#endif
				Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::InterpolateRT);
				RHICmdList.EnqueueLambda(
					[PrepareDesc,
						ConfigureDesc,
						ViewportRHI,
						Presenter,
						Context,
						InterpolatedRes,
						interpolateParams,
						OutputExtents,
						OutputPoint,
						bWholeScreen,
						ViewportOutputFormat,
						GHDRMinLuminnanceLog10,
						GHDRMaxLuminnance
					](FRHICommandListImmediate& cmd) mutable
					{
						if (Context->Context)
						{
							ConfigureDesc.swapChain = nullptr;
							ConfigureDesc.presentCallback = nullptr;
							auto Code = Presenter->GetBackend()->ffxConfigure(&Context->Context, &ConfigureDesc.header);
							check(Code == FFX_API_RETURN_OK);
						}

					Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::InterpolateRHI);

					FfxCommandList CmdBuffer = nullptr;
					{
						CmdBuffer = Presenter->GetBackend()->GetNativeCommandBuffer(cmd);
					}
					if (CmdBuffer)
					{
						// Prepare the interpolation context on the current RHI command list
						{
							ffxApiDispatchDescFrameGenerationPrepare UpscalerDesc = PrepareDesc;
							UpscalerDesc.commandList = Presenter->GetBackend()->GetNativeCommandBuffer(cmd);

							auto Code =
								Presenter->GetBackend()->ffxDispatch(&Context->Context, &UpscalerDesc.header);
							check(Code == FFX_API_RETURN_OK);
						}

						// Interpolate the frame
						{
							interpolateParams->outputs[0] = InterpolatedRes;
							interpolateParams->commandList = CmdBuffer;

							auto Code =
								Presenter->GetBackend()->ffxDispatch(&Context->Context, &interpolateParams->header);
							check(Code == FFX_API_RETURN_OK);
						}
					}
					delete interpolateParams;
				});

				if (bUseFragmentShader)
				{
					Presenter->GetBackend()->RestoreGraphicsStateAfterExternalWork(
						RHICmdList, FIntRect(OutputPoint, OutputPoint + OutputExtents));
				}
				else
				{
					Presenter->GetBackend()->Flush(RHICmdList);
				}
				Context->FrameSync.EndWork(RHICmdList);

			});

			// RDG must see the copy separately from the SDK's RTV/UAV writes.
			if (!bDirectToBackBuffer)
			{
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.DestPosition = FIntVector(OutputPoint.X, OutputPoint.Y, 0);
				CopyInfo.Size = FIntVector(OutputExtents.X, OutputExtents.Y, 1);
				if (!bWholeScreen)
				{
					AddCopyTexturePass(GraphBuilder, InterBuffer, InterpolatedRDG, CopyInfo);
				}
				if (bWholeScreen || Presenter->GetMode() == ENFRUPresentModeRHI)
				{
					AddCopyTexturePass(GraphBuilder, InterBuffer, BackBufferRDG, CopyInfo);
				}
			}

			if (!bSDKManagesDepth)
			{
				// The lane conversion already wrote the current depth directly into the
				// next history target; no Depth copy is required.
				Context->DepthHistoryIndex = DepthWriteIndex;
			}
		}
		else
		{
			delete interpolateParams;
		}
	}; // close DoAddPass lambda

	if (bUseFragmentShader)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<NFRUPass::FFragmentParameters>();
		PassParameters->BackBufferTexture = bDirectToBackBuffer ? BackBufferRDG : nullptr;
		DoAddPass(PassParameters, PassParameters->BackBufferTexture);
	}
	else
	{
		DoAddPass(GraphBuilder.AllocParameters<NFRUPass::FComputeParameters>(), nullptr);
	}

	return bInterpolated;
}
// clang-format on

#if UE_VERSION_OLDER_THAN(5, 0, 0)
inline static FRDGTextureRef RegisterExternalTexture(
	FRDGBuilder& GraphBuilder, FRHITexture* Texture, const TCHAR* NameIfUnregistered = nullptr)
{
	if (FRDGTextureRef FoundTexture = GraphBuilder.FindExternalTexture(Texture))
	{
		return FoundTexture;
	}

	return GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, NameIfUnregistered));
}
#endif

void NFRU::InterpolateFrame(FRDGBuilder& InGraphBuilder)
{
	auto* Engine = GEngine;
	auto GameViewport = Engine ? Engine->GameViewport : nullptr;
	auto Viewport = GameViewport ? GameViewport->Viewport : nullptr;
	auto ViewportRHI = Viewport ? Viewport->GetViewportRHI() : nullptr;
	FIntPoint ViewportSizeXY = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	NFRUCustomPresent* Presenter =
		ViewportRHI.IsValid() ? (NFRUCustomPresent*)ViewportRHI->GetCustomPresent() : nullptr;
	INGSharedBackendModule* VkBackend = FModuleManager::GetModulePtr<INGSharedBackendModule>(TEXT("NGVulkanBackend"));
	INGSharedBackend* VkBackendInstance = VkBackend ? VkBackend->GetBackend() : nullptr;
	bool bAllowed = CVarEnableNFRU.GetValueOnAnyThread() != 0 && Presenter && (Views.Num() > 0);
	bool bNeuralGraphicSupported = VkBackendInstance && VkBackendInstance->IsNeuralGraphicSupported();
	bAllowed &= bNeuralGraphicSupported;

	bool bWarningLogged = false;
	if (!bWarningLogged && CVarEnableNFRU.GetValueOnAnyThread() != 0 && VkBackendInstance && !bNeuralGraphicSupported)
	{
		UE_LOG(LogNFRU,
			Warning,
			TEXT("NFRU: Current device does not support Neural Graphic. Frame interpolation is disabled."));
		bWarningLogged = true;
	}

#if WITH_EDITORONLY_DATA
	bAllowed &= !GIsEditor;
#endif
#if UE_VERSION_AT_LEAST(5, 0, 0)
	FRDGBuilder& GraphBuilder = InGraphBuilder;
#else
	if (INGSharedBackend::GetGraphBuilder() == nullptr)
	{
		bAllowed = false;
	}
#endif

	if (bAllowed)
	{
		FTextureRHIRef BackBuffer = RHIGetViewportBackBuffer(ViewportRHI);
#if UE_VERSION_AT_LEAST(5, 0, 0)
		FRDGTextureRef BackBufferRDG = RegisterExternalTexture(GraphBuilder, BackBuffer, nullptr);
#else
		FRDGBuilder& GraphBuilder = *INGSharedBackend::GetGraphBuilder();
		FRDGTextureRef BackBufferRDG = RegisterExternalTexture(GraphBuilder, BackBuffer);
#endif

#if UE_VERSION_AT_LEAST(5, 0, 0)
		ETextureCreateFlags DescFlags = TexCreate_ShaderResource;
#else
		ETextureCreateFlags DescFlags = TexCreate_None;
#endif

		const bool bUseFragmentShader = CVarNFRUUseFragmentShader.GetValueOnAnyThread() != 0;
		bool bNeedRecreate = (bLastUseFragmentShader != bUseFragmentShader);
		for (int i = 0; i < 2; i++)
		{
			if (bNeedRecreate)
			{
				break;
			}
			if (!IsValidRef(BackBufferRT[i]) || BackBufferRT[i]->GetDesc().Extent.X != BackBufferRDG->Desc.Extent.X
				|| BackBufferRT[i]->GetDesc().Extent.Y != BackBufferRDG->Desc.Extent.Y
				|| BackBufferRT[i]->GetDesc().Format != BackBufferRDG->Desc.Format)
			{
				bNeedRecreate = true;
				break;
			}
		}

		if (bNeedRecreate)
		{
			bLastUseFragmentShader = bUseFragmentShader;
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(BackBufferRDG->Desc.Extent.X, BackBufferRDG->Desc.Extent.Y),
				BackBuffer->GetFormat(),
				FClearValueBinding::Transparent,
				DescFlags,
				bUseFragmentShader ? TexCreate_RenderTargetable : TexCreate_UAV,
				false,
				1,
				true,
				true));
			for (int i = 0; i < 2; i++)
			{
				GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, BackBufferRT[i], TEXT("BackBufferRT"));
			}
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, InterpolatedRT, TEXT("InterpolatedRT"));
		}

		Presenter->BeginFrame();
		Presenter->SetPreUITextures(BackBufferRT[Index], InterpolatedRT);
		Presenter->SetEnabled(true);

		FRHICopyTextureInfo Info;
		// Current color buffer
		FRDGTextureRef FinalBuffer = GraphBuilder.RegisterExternalTexture(BackBufferRT[Index]);
		// History color buffer
		FRDGTextureRef ColorBufferTm1 = GraphBuilder.RegisterExternalTexture(BackBufferRT[(Index + 1) % 2]);
		FRDGTextureRef InterpolatedRDG = GraphBuilder.RegisterExternalTexture(InterpolatedRT);
		check(BackBufferRDG->Desc.Extent == FinalBuffer->Desc.Extent
			  && BackBufferRDG->Desc.Extent == ColorBufferTm1->Desc.Extent);
		// Copy current back buffer to current color buffer, it will be next frame history color buffer.
		AddCopyTexturePass(GraphBuilder, BackBufferRDG, FinalBuffer, Info);

		bAllowed = false;
		uint32 InterpolateIndex = 0;
		for (auto Pair : Views)
		{
			if (Pair.Key->State)
			{
				if (Pair.Value.bEnabled && Pair.Value.ViewFamilyTexture
					&& (ViewportSizeXY.X == Pair.Value.ViewFamilyTexture->Desc.Extent.X)
					&& (ViewportSizeXY.Y == Pair.Value.ViewFamilyTexture->Desc.Extent.Y))
				{
					bool const bInterpolated = InterpolateView(GraphBuilder,
						Presenter,
						Pair.Key,
						Pair.Value,
						FinalBuffer,
						ColorBufferTm1,
						InterpolatedRDG,
						BackBufferRDG,
						InterpolateIndex);
					InterpolateIndex = bInterpolated ? InterpolateIndex + 1 : InterpolateIndex;
					bAllowed |= bInterpolated;
				}
			}
		}

		Presenter->EndFrame();
		// Switch the index.
		Index = (Index + 1) % 2;
	}

	if (CVarEnableNFRU.GetValueOnAnyThread() == 0 && Presenter && Presenter->Enabled())
	{
		Presenter->SetEnabled(false);
	}

	Views.Empty();

	bInterpolatedFrame |= bAllowed;
	ResetState = bInterpolatedFrame ? 3u : 0u;
}

void NFRU::OnSlateWindowRendered(SWindow& SlateWindow, void* ViewportRHIPtr)
{
	static bool bProcessing = false;

	FViewportRHIRef Viewport = *((FViewportRHIRef*)ViewportRHIPtr);
	NFRUCustomPresent* PresentHandler = (NFRUCustomPresent*)Viewport->GetCustomPresent();

	if (IsInGameThread() && PresentHandler && PresentHandler->Enabled() && CVarEnableNFRU.GetValueOnAnyThread())
	{
		if (!bProcessing)
		{
			bProcessing = true;
			FSlateApplication& App = FSlateApplication::Get();
			TSharedPtr<SWindow> WindowPtr;
			TSharedPtr<SWidget> TestWidget = SlateWindow.AsShared();
			while (TestWidget && !WindowPtr.IsValid())
			{
				if (TestWidget->Advanced_IsWindow())
				{
					WindowPtr = StaticCastSharedPtr<SWindow>(TestWidget);
				}

				TestWidget = TestWidget->GetParentWidget();
			}

			Windows.Add(&SlateWindow, Viewport);
			bool bDrawDebugView = false;
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
			bDrawDebugView = CVarNFRUShowDebugView.GetValueOnAnyThread() != 0;
#endif

			if (PresentHandler->GetMode() == ENFRUPresentModeRHI && !bDrawDebugView)
			{
				ENQUEUE_RENDER_COMMAND(UpdateWindowBackBufferCommand)
				(
					[this, Viewport](FRHICommandListImmediate& RHICmdList)
					{
#if UE_VERSION_AT_LEAST(5, 2, 0)
						FTextureRHIRef BackBuffer = RHIGetViewportBackBuffer(Viewport);
#else
						FTextureRHIRef BackBuffer = RHICmdList.GetViewportBackBuffer(Viewport);
#endif
						NFRUCustomPresent* Presenter = (NFRUCustomPresent*)Viewport->GetCustomPresent();
						// The INdex has been flipped in the presenter already, so we need to use 1-Index to get the
						// currently rendered frame.
						if (Presenter && BackBufferRT[1 - Index].IsValid())
						{
							this->CalculateFPSTimings();
							FTextureRHIRef RealFrameBackup = BackBufferRT[1 - Index]->GetRHI(GET_RHI_TARGET_ARG);
							{
#if UE_VERSION_AT_LEAST(5, 5, 0)
								SCOPED_DRAW_EVENT(RHICmdList, NFRU::OnSlateWindowRendered)
#else
								RHICmdList.PushEvent(TEXT("NFRU::OnSlateWindowRendered"), FColor::White);
#endif
								check(FIntPoint(RealFrameBackup->GetSizeXYZ().X, RealFrameBackup->GetSizeXYZ().Y)
									  == FIntPoint(BackBuffer->GetSizeXYZ().X, BackBuffer->GetSizeXYZ().Y));
								TransitionAndCopyTexture(RHICmdList, RealFrameBackup, BackBuffer, {});
#if UE_VERSION_OLDER_THAN(5, 5, 0)
								RHICmdList.PopEvent();
#endif
							}

							Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::PresentRT);
							RHICmdList.EnqueueLambda([this, Presenter](FRHICommandListImmediate& cmd) mutable
								{ Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::PresentRHI); });
						}
					});

				double OldLastTickTime = 0.0;
				double& SlateLastTickTimeRef = ((FFXSlateApplication&)App).LastTickTimeRef();

				bool const bModifySlateDeltaTime = (CVarNFRUModifySlateDeltaTime.GetValueOnAnyThread() != 0);
				if (bModifySlateDeltaTime)
				{
					OldLastTickTime = SlateLastTickTimeRef;
					SlateLastTickTimeRef = App.GetCurrentTime();
				}

				// If we hold on to this and the viewport resizes during redrawing then bad things will happen.
				Viewport.SafeRelease();

				// This is only to show the interpolated frames in the game window.
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
				if (CVarNFRUOnlyInterpolatedFrames.GetValueOnAnyThread() == 0)
#endif
				{
					App.ForceRedrawWindow(WindowPtr.ToSharedRef());
				}

				if (bModifySlateDeltaTime)
				{
					SlateLastTickTimeRef = OldLastTickTime;
				}
			}
			bProcessing = false;
		}
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(UpdateWindowBackBufferCommand)
		(
			[Viewport](FRHICommandListImmediate& RHICmdList)
			{
				NFRUCustomPresent* Presenter = (NFRUCustomPresent*)Viewport->GetCustomPresent();
				if (Presenter)
				{
					Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::PresentRT);
					RHICmdList.EnqueueLambda([Presenter](FRHICommandListImmediate& cmd) mutable
						{ Presenter->SetCustomPresentStatus(NFRUCustomPresentStatus::PresentRHI); });
				}
			});
	}
}

void NFRU::OnPreResizeWindowBackBufferCallback(void* ViewportRHIPtr)
{
	const FViewportRHIRef& Viewport = *static_cast<FViewportRHIRef*>(ViewportRHIPtr);
	for (auto& Window : Windows)
	{
		if (Window.Value.IsValid() && Window.Value == Viewport)
		{
			ResizingWindows.Add(Window.Key, ViewportRHIPtr);
			Window.Value.SafeRelease();
		}
	}
}

void NFRU::OnPostResizeWindowBackBufferCallback(void* ViewportRHIPtr)
{
	for (auto It = ResizingWindows.CreateIterator(); It; ++It)
	{
		if (It.Value() == ViewportRHIPtr)
		{
			Windows.FindChecked(It.Key()) = *static_cast<FViewportRHIRef*>(ViewportRHIPtr);
			It.RemoveCurrent();
		}
	}
}

void NFRU::OnBackBufferReadyToPresentCallback(class SWindow& SlateWindow, const FTextureRHIRef& BackBuffer)
{
	/** Callback for when a backbuffer is ready for reading (called on render thread) */
	FViewportRHIRef* ViewportPtr = Windows.Find(&SlateWindow);
	FViewportRHIRef Viewport = ViewportPtr ? *ViewportPtr : nullptr;
	NFRUCustomPresent* Presenter = Viewport ? (NFRUCustomPresent*)Viewport->GetCustomPresent() : nullptr;

	ResetState = (ResetState > 0) ? ResetState - 1 : ResetState;

	if (Presenter && CVarEnableNFRU.GetValueOnAnyThread())
	{
		if (ResetState)
		{
			Presenter->CopyBackBufferRT(BackBuffer);
		}
	}

	bInterpolatedFrame = false;
}
