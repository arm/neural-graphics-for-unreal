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

#include "NFRUCustomPresent.h"

#include "GlobalShader.h"
#include "NGSettings.h"
#include "PipelineStateCache.h"
#include "RenderTargetPool.h"
#include "ShaderCompilerCore.h"

#include "NFRUStats.h"

#if UE_VERSION_AT_LEAST(5, 0, 0)
#include "ShaderCompilerCore.h"
#else
#include "ShaderParameterUtils.h"
#endif

#if UE_VERSION_AT_LEAST(5, 2, 0)
#include "DataDrivenShaderPlatformInfo.h"
#else
#include "RHIDefinitions.h"
#endif

#if UE_VERSION_OLDER_THAN(5, 0, 0)
typedef FIntPoint FUintVector2;
#endif

namespace
{
	static TArray<int32> GCachedFpsLevels;
	void InitFpsLevels(int32 MaxFPS)
	{
		if (GCachedFpsLevels.Num() == 0)
		{
			GCachedFpsLevels.Add(MaxFPS);
			for (int32 Div = 2;; ++Div)
			{
				int32 Level = MaxFPS / Div;
				if (Level < 10)
					break;
				if (!GCachedFpsLevels.Contains(Level))
					GCachedFpsLevels.Add(Level);
			}
			GCachedFpsLevels.Sort();
		}
	}
}

//------------------------------------------------------------------------------------------------------
// Unreal shader to copy additional UI that only renders on the first invocation of Slate such as debug UI.
//------------------------------------------------------------------------------------------------------
class FFXFIAdditionalUICS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFXFIAdditionalUICS, Global);

public:
	static const int ThreadgroupSizeX = 8;
	static const int ThreadgroupSizeY = 8;
	static const int ThreadgroupSizeZ = 1;

	FFXFIAdditionalUICS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer)
	{
		FirstFrame.Bind(Initializer.ParameterMap, TEXT("FirstFrame"));
		FirstFrameWithUI.Bind(Initializer.ParameterMap, TEXT("FirstFrameWithUI"));
		SecondFrame.Bind(Initializer.ParameterMap, TEXT("SecondFrame"));
		SecondFrameWithUI.Bind(Initializer.ParameterMap, TEXT("SecondFrameWithUI"));
		ViewSize.Bind(Initializer.ParameterMap, TEXT("ViewSize"));
		ViewMin.Bind(Initializer.ParameterMap, TEXT("ViewMin"));
	}
	FFXFIAdditionalUICS() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
#if UE_VERSION_AT_LEAST(5, 0, 0)
		OutEnvironment.SetDefine(TEXT("UNREAL_VERSION"), 5);
#else
		OutEnvironment.SetDefine(TEXT("UNREAL_VERSION"), 4);
#endif
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadgroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadgroupSizeY);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), ThreadgroupSizeZ);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("UNREAL_ENGINE_MAJOR_VERSION"), ENGINE_MAJOR_VERSION);
	}

	void SetParameters(FRHICommandList& RHICmdList,
		FUintVector2 InViewSize,
		FUintVector2 InViewMin,
		FRHITexture* InFirstFrame,
		FRHITexture* InFirstFrameWithUI,
		FRHITexture* InSecondFrame,
		FRHIUnorderedAccessView* InSecondFrameWithUI)
	{
#if UE_VERSION_AT_LEAST(5, 3, 0)
		FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
		SetShaderValue(BatchedParameters, ViewSize, InViewSize, 0);
		SetShaderValue(BatchedParameters, ViewMin, InViewMin, 0);
		SetTextureParameter(BatchedParameters, FirstFrame, InFirstFrame);
		SetTextureParameter(BatchedParameters, FirstFrameWithUI, InFirstFrameWithUI);
		SetTextureParameter(BatchedParameters, SecondFrame, InSecondFrame);
		SetUAVParameter(BatchedParameters, SecondFrameWithUI, InSecondFrameWithUI);
		RHICmdList.SetBatchedShaderParameters(RHICmdList.GetBoundComputeShader(), BatchedParameters);
#else
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ViewSize, InViewSize);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ViewMin, InViewMin);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), FirstFrame, InFirstFrame);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), FirstFrameWithUI, InFirstFrameWithUI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), SecondFrame, InSecondFrame);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), SecondFrameWithUI, InSecondFrameWithUI);
#endif
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Plugin/NSS/Private/PostProcessFFX_FIAdditionalUI.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainCS");
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, FirstFrame);
	LAYOUT_FIELD(FShaderResourceParameter, FirstFrameWithUI);
	LAYOUT_FIELD(FShaderResourceParameter, SecondFrame);
	LAYOUT_FIELD(FShaderResourceParameter, SecondFrameWithUI);
	LAYOUT_FIELD(FShaderParameter, ViewSize);
	LAYOUT_FIELD(FShaderParameter, ViewMin);
};
IMPLEMENT_SHADER_TYPE(
	, FFXFIAdditionalUICS, TEXT("/Plugin/NG/Private/PostProcessFFX_FIAdditionalUI.usf"), TEXT("MainCS"), SF_Compute);

//------------------------------------------------------------------------------------------------------
// Implementation for NFRUResources
//------------------------------------------------------------------------------------------------------
NFRUResources::NFRUResources(INGSharedBackend* InBackend, uint32 InUniqueID)
	: FRHIResource(RRT_None), UniqueID(InUniqueID), Context(nullptr), Backend(InBackend), bDebugView(false)
{
	FMemory::Memzero(Desc);
}

NFRUResources::~NFRUResources()
{
	if (Backend != nullptr && Context != nullptr)
	{
		Backend->ffxDestroyContext(&Context);
	}
}

//------------------------------------------------------------------------------------------------------
// Implementation for NFRUCustomPresent
//------------------------------------------------------------------------------------------------------
FFXFIResourceRef NFRUCustomPresent::UpdateContexts(
	FRDGBuilder& GraphBuilder, uint32 UniqueID, ffxApiCreateContextDescFrameGeneration const& FgDesc)
{
	bool bResourcesValid = false;
	FFXFIResourceRef Resource;

	if (!bResized)
	{
		for (auto& Existing : OldResources)
		{
			bResourcesValid = Existing->UniqueID == UniqueID;
			if (bResourcesValid)
			{
				Resource = Existing;
				break;
			}
		}

		if (bResourcesValid)
		{
			bResourcesValid &= Resource->Desc.displaySize.width == FgDesc.displaySize.width;
			bResourcesValid &= Resource->Desc.displaySize.height == FgDesc.displaySize.height;
			bResourcesValid &= Resource->Desc.renderSize.width == FgDesc.renderSize.width;
			bResourcesValid &= Resource->Desc.renderSize.height == FgDesc.renderSize.height;
			bResourcesValid &= Resource->Desc.backBufferFormat == FgDesc.backBufferFormat;
			bResourcesValid &= Resource->Desc.flags == FgDesc.flags;
		}
	}
	else
	{
		bResized = false;
	}

	if (!bResourcesValid)
	{
		Resource = new NFRUResources(Backend, UniqueID);
		Resource->Desc = FgDesc;

		auto Code = Backend->ffxCreateContext(&Resource->Context, &Resource->Desc.header);
		if (Code != FFX_API_RETURN_OK)
		{
			Resource.SafeRelease();
		}
	}
	CurrentResource = Resource;
	if (CurrentResource.IsValid())
	{
		Resources.Add(CurrentResource);
	}
	check(CurrentResource.IsValid() && Resources.Num() > 0);
	return Resource;
}

NFRUCustomPresent::NFRUCustomPresent()
	: Backend(nullptr)
	, Viewport(nullptr)
	, RHIViewport(nullptr)
	, Status(NFRUCustomPresentStatus::PresentRT)
	, Mode(ENFRUPresentModeRHI)
	, Api(EFFXBackendAPI::Unknown)
	, bNeedsNativePresentRT(false)
	, bPresentRHI(false)
	, bHasValidInterpolatedRT(false)
	, bEnabled(false)
	, bResized(false)
	, bUseFFXSwapchain(false)
	, bHasInterpolatedRT(false)
	, bHasInterpolatedRHI(false)
	, LastTime(FPlatformTime::Seconds())
	, AverageTime(0.0f)
	, AverageFPS(0.0f)

{
	FMemory::Memzero(Desc);
	RHIFrameTime = (FPlatformTime::Seconds() - GStartTime);
	int32 MaxFPS = FPlatformRHIFramePacer::GetFramePace();
	InitFpsLevels(MaxFPS);
}

NFRUCustomPresent::~NFRUCustomPresent() {}

void NFRUCustomPresent::InitViewport(FViewport* InViewport, FViewportRHIRef ViewportRHI)
{
	Viewport = InViewport;
	RHIViewport = ViewportRHI;
	RHIViewport->SetCustomPresent(this);
}

bool NFRUCustomPresent::InitSwapChain(INGSharedBackend* InBackend,
	uint32_t Flags,
	FIntPoint RenderSize,
	FIntPoint DisplaySize,
	FfxSwapchain RawSwapChain,
	FfxCommandQueue Queue,
	FfxApiSurfaceFormat Format,
	EFFXBackendAPI InApi)
{
	Api = InApi;

	FfxErrorCode Result = FFX_OK;
	if (Backend != InBackend || Desc.flags != Flags || Desc.renderSize.width != RenderSize.X
		|| Desc.renderSize.height != RenderSize.Y || Desc.displaySize.width != DisplaySize.X
		|| Desc.displaySize.height != DisplaySize.Y || Format != Desc.backBufferFormat)
	{
		Desc.flags = Flags;
		Desc.renderSize.width = RenderSize.X;
		Desc.renderSize.height = RenderSize.Y;
		Desc.displaySize.width = DisplaySize.X;
		Desc.displaySize.height = DisplaySize.Y;
		Desc.backBufferFormat = Format;

		Backend = InBackend;
	}

	return (Result == FFX_OK);
}

// Called when viewport is resized.
void NFRUCustomPresent::OnBackBufferResize()
{
	bResized = true;
	// Flush the outstanding GPU work and wait for it to complete.
	FlushRenderingCommands();
	FRHICommandListExecutor::CheckNoOutstandingCmdLists();
}

// Called from render thread to see if a native present will be requested for this frame.
// @return	true if native Present will be requested for this frame; false otherwise.  Must
// match value subsequently returned by Present for this frame.
bool NFRUCustomPresent::NeedsNativePresent()
{
	if (Status == NFRUCustomPresentStatus::PresentRT && (!bUseFFXSwapchain || bNeedsNativePresentRT))
	{
		if (!bHasInterpolatedRT)
		{
			SetEnabled(false);
		}
		bHasInterpolatedRT = false;
	}

	return bUseFFXSwapchain ? bNeedsNativePresentRT : true;
}
// In come cases we want to use custom present but still let the native environment handle
// advancement of the backbuffer indices.
// @return true if backbuffer index should advance independently from CustomPresent.
bool NFRUCustomPresent::NeedsAdvanceBackbuffer()
{
	return false;
}

// Called from RHI thread when the engine begins drawing to the viewport.
void NFRUCustomPresent::BeginDrawing() {}

void NFRUCustomPresent::CalculateFPSTimings()
{
	double CurrentTime = FPlatformTime::Seconds();
	float FrameTimeMS = (float)((CurrentTime - LastTime) * 1000.0);
	AverageTime = AverageTime * 0.9f + FrameTimeMS * 0.1f;
	AverageFPS = 1000.f / AverageTime;
	LastTime = CurrentTime;
}

void NFRUCustomPresent::GetAverageFrameTimes(float& AvgTimeMs, float& AvgFPS)
{
	AvgTimeMs = AverageTime;
	AvgFPS = AverageFPS;
}

void NFRUCustomPresent::CustomFramePacerAdjust()
{
	if (ShouldUseCustomFramePacing())
	{
		double NowCPUTime = (FPlatformTime::Seconds() - GStartTime);
		if (!FMath::IsFinite(RHIFrameTime))
		{
			RHIFrameTime = NowCPUTime;
		}
		double TimeToSleep = (RHIFrameTime - NowCPUTime);
		double TargetIntervalWithEpsilon = 1.0 / (double)CustomFPS;
		RHIFrameTime = FMath::Max((RHIFrameTime + TargetIntervalWithEpsilon), NowCPUTime);

		const TArray<int32>& FpsLevels = GCachedFpsLevels;
		int32 Index = FpsLevels.IndexOfByKey(CustomFPS);
		// Only frame rates that meet the conditions will be considered.
		// Sleep time must be continuously greater than 0 and greater than the interval of the next frame rate.
		if (TimeToSleep > 0 && Index != INDEX_NONE && Index + 1 < FpsLevels.Num())
		{
			Downcount = 0;
			int32 NextLevel = FpsLevels[Index + 1];
			double NextInterval = 1.0 / NextLevel;

			if (TimeToSleep > NextInterval)
				Upcount++;
			else
				Upcount = 0;

			if (Upcount >= CVarNFRUUpAdjustFrameCount.GetValueOnAnyThread())
			{
				SetCustomFPS(NextLevel);
				Upcount = 0;
			}
		}

		if (TimeToSleep < 0 && Index != INDEX_NONE && Index - 1 >= 0)
		{
			Upcount = 0;
			Downcount++;
			if (Downcount >= CVarNFRUDownAdjustFrameCount.GetValueOnAnyThread())
			{
				int32 PrevLevel = FpsLevels[Index - 1];
				SetCustomFPS(PrevLevel);
				Downcount = 0;
			}
		}
	}
}
// Called from RHI thread to perform custom present.
// @param InOutSyncInterval - in out param, indicates if vsync is on (>0) or off (==0).
// @return	true if native Present should be also be performed; false otherwise. If it returns
// true, then InOutSyncInterval could be modified to switch between VSync/NoVSync for the normal
// Present.  Must match value previously returned by NeedsNativePresent for this frame.

// InOutSyncInterval is currently unused; Unreal Engine does not process this parameter.
// TODO: Handle sync interval if required in the future.
// TODO: keep this function for the future if we need to add more logic here.

bool NFRUCustomPresent::Present(int32& InOutSyncInterval)
{
	this->CalculateFPSTimings();
	CustomFramePacerAdjust();

	bool bDrawDebugView = false;
#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
	bDrawDebugView = CVarNFRUShowDebugView.GetValueOnAnyThread() != 0;
#endif
	// Currently, always true as the plugin uses UE's native Swapchain.
	return (!bUseFFXSwapchain || bDrawDebugView);
}

// Called from RHI thread after native Present has been called
void NFRUCustomPresent::PostPresent() {}

// Called when rendering thread is acquired
void NFRUCustomPresent::OnAcquireThreadOwnership() {}

// Called when rendering thread is released
void NFRUCustomPresent::OnReleaseThreadOwnership() {}

void NFRUCustomPresent::CopyBackBufferRT(FTextureRHIRef InBackBuffer)
{
	if (!Enabled())
	{
		return;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	FRHICopyTextureInfo Info;
	Info.Size.X = InBackBuffer->GetSizeX();
	Info.Size.Y = InBackBuffer->GetSizeY();

#if UE_VERSION_AT_LEAST(5, 0, 0)
	ETextureCreateFlags DescFlags = TexCreate_UAV;
#else
	ETextureCreateFlags DescFlags = TexCreate_None;
#endif
	FPooledRenderTargetDesc RTDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(Info.Size.X, Info.Size.Y),
		InBackBuffer->GetFormat(),
		FClearValueBinding::Transparent,
		DescFlags,
		TexCreate_UAV | TexCreate_ShaderResource,
		false,
		1,
		true,
		true));

	switch (Status)
	{
		case NFRUCustomPresentStatus::InterpolateRT:
		{
			check(Mode == ENFRUPresentModeRHI);
			if (CVarNFRUCaptureDebugUI.GetValueOnAnyThread())
			{
				auto& Dest = Current.Interpolated;
				GRenderTargetPool.FindFreeElement(RHICmdList, RTDesc, Dest, TEXT("Interpolated"));
				check(FIntPoint(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y) == Dest->GetDesc().Extent);
#if UE_VERSION_AT_LEAST(5, 0, 0)
				RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(Dest->GetRHI(), ERHIAccess::Unknown, ERHIAccess::CopyDest)});

				RHICmdList.CopyTexture(InBackBuffer, Dest->GetRHI(), Info);

				RHICmdList.Transition({
					FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::Present),
					FRHITransitionInfo(Dest->GetRHI(), ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				});
#else
				RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(
						Dest->GetRHI(ERenderTargetTexture::Targetable), ERHIAccess::Unknown, ERHIAccess::CopyDest)});

				RHICmdList.CopyTexture(InBackBuffer, Dest->GetRHI(ERenderTargetTexture::Targetable), Info);

				RHICmdList.Transition({
					FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::Present),
					FRHITransitionInfo(
						Dest->GetRHI(ERenderTargetTexture::Targetable), ERHIAccess::Unknown, ERHIAccess::SRVCompute),
				});
#endif
				bHasValidInterpolatedRT = true;
			}
			else
			{
				RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::Present)});
				bHasValidInterpolatedRT = false;
			}
			break;
		}
		case NFRUCustomPresentStatus::PresentRT:
		{
			RHICmdList.PushEvent(TEXT("NFRUCustomPresent::CopyBackBufferRT PresentRT"), FColor::White);

			if (CVarNFRUCaptureDebugUI.GetValueOnAnyThread() && bHasValidInterpolatedRT
				&& (Mode == ENFRUPresentModeRHI))
			{
				auto& SecondFrameUI = Current.RealFrame;
				GRenderTargetPool.FindFreeElement(RHICmdList, RTDesc, SecondFrameUI, TEXT("RealFrame"));
#if UE_VERSION_AT_LEAST(5, 0, 0)
				RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(SecondFrameUI->GetRHI(), ERHIAccess::Unknown, ERHIAccess::CopyDest)});

				check(FIntPoint(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y)
					  == SecondFrameUI->GetDesc().Extent);
				RHICmdList.CopyTexture(InBackBuffer, SecondFrameUI->GetRHI(), Info);
#else
				RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(SecondFrameUI->GetRHI(ERenderTargetTexture::Targetable),
						ERHIAccess::Unknown,
						ERHIAccess::CopyDest)});

				check(FIntPoint(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y)
					  == SecondFrameUI->GetDesc().Extent);
				RHICmdList.CopyTexture(InBackBuffer, SecondFrameUI->GetRHI(ERenderTargetTexture::Targetable), Info);
#endif

				auto& FirstFrame = InterpolatedNoUI;
				auto& SecondFrame = RealFrameNoUI;
				auto& FirstFrameUI = Current.Interpolated;

#if UE_VERSION_AT_LEAST(5, 3, 0)
				auto RWSecondFrameUI = FRHICommandListExecutor::GetImmediateCommandList().CreateUnorderedAccessView(
					SecondFrameUI->GetRHI());
#elif UE_VERSION_AT_LEAST(5, 0, 0)
				auto RWSecondFrameUI = RHICreateUnorderedAccessView(SecondFrameUI->GetRHI());
#else
				auto RWSecondFrameUI =
					RHICreateUnorderedAccessView(SecondFrameUI->GetRHI(ERenderTargetTexture::Targetable));
#endif

				TShaderRef<FFXFIAdditionalUICS> ComputeShader =
					TShaderMapRef<FFXFIAdditionalUICS>(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				RHICmdList.Transition({
					FRHITransitionInfo(RWSecondFrameUI, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				});

				FIntPoint Extent(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y);
				SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
#if UE_VERSION_AT_LEAST(5, 0, 0)
				ComputeShader->SetParameters(RHICmdList,
					FUintVector2(Extent.X, Extent.Y),
					FUintVector2(0, 0),
					FirstFrame->GetRHI(),
					FirstFrameUI->GetRHI(),
					SecondFrame->GetRHI(),
					RWSecondFrameUI);

				RHICmdList.DispatchComputeShader(
					FMath::DivideAndRoundUp(Extent.X, FFXFIAdditionalUICS::ThreadgroupSizeX),
					FMath::DivideAndRoundUp(Extent.Y, FFXFIAdditionalUICS::ThreadgroupSizeY),
					1);

				RHICmdList.Transition(
					{FRHITransitionInfo(SecondFrameUI->GetRHI(), ERHIAccess::Unknown, ERHIAccess::CopySrc),
						FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopyDest)});

				check(SecondFrameUI->GetDesc().Extent
					  == FIntPoint(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y));

				RHICmdList.CopyTexture(SecondFrameUI->GetRHI(), InBackBuffer, Info);
#else
				ComputeShader->SetParameters(RHICmdList,
					FIntPoint(Extent.X, Extent.Y),
					FIntPoint(0, 0),
					FirstFrame->GetRHI(ERenderTargetTexture::Targetable),
					FirstFrameUI->GetRHI(ERenderTargetTexture::Targetable),
					SecondFrame->GetRHI(ERenderTargetTexture::Targetable),
					RWSecondFrameUI);

				RHICmdList.DispatchComputeShader(
					FMath::DivideAndRoundUp(Extent.X, FFXFIAdditionalUICS::ThreadgroupSizeX),
					FMath::DivideAndRoundUp(Extent.Y, FFXFIAdditionalUICS::ThreadgroupSizeY),
					1);

				RHICmdList.Transition({FRHITransitionInfo(SecondFrameUI->GetRHI(ERenderTargetTexture::Targetable),
										   ERHIAccess::Unknown,
										   ERHIAccess::CopySrc),
					FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::CopyDest)});

				check(SecondFrameUI->GetDesc().Extent
					  == FIntPoint(InBackBuffer->GetSizeXYZ().X, InBackBuffer->GetSizeXYZ().Y));

				RHICmdList.CopyTexture(SecondFrameUI->GetRHI(ERenderTargetTexture::Targetable), InBackBuffer, Info);
#endif
			}

			RHICmdList.Transition({FRHITransitionInfo(InBackBuffer, ERHIAccess::Unknown, ERHIAccess::Present)});

			bHasValidInterpolatedRT = false;

			RHICmdList.PopEvent();
			break;
		}
		default:
		{
			break;
		}
	}
}

void NFRUCustomPresent::SetMode(ENFRUPresentMode InMode)
{
	Mode = InMode;
}

void NFRUCustomPresent::SetEnabled(bool const bInEnabled)
{
	bEnabled = bInEnabled;
}

void NFRUCustomPresent::SetCustomPresentStatus(NFRUCustomPresentStatus Flag)
{
	switch (Flag)
	{
		case NFRUCustomPresentStatus::InterpolateRT:
		{
			bHasInterpolatedRT = true;

			Status = Flag;
			bNeedsNativePresentRT = false;
			break;
		}
		case NFRUCustomPresentStatus::PresentRT:
		{
			Status = NFRUCustomPresentStatus::PresentRT;
			bNeedsNativePresentRT = true;
			break;
		}
		case NFRUCustomPresentStatus::InterpolateRHI:
		{
			bHasInterpolatedRHI = true;

			bPresentRHI = false;
			break;
		}
		case NFRUCustomPresentStatus::PresentRHI:
		{
			bPresentRHI = true;
			break;
		}
	}
}

void NFRUCustomPresent::SetUseFFXSwapchain(bool const bToggle)
{
	bUseFFXSwapchain = bToggle;
}
