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

// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "NSS.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/IConsoleManager.h"
#include "LegacyScreenPercentageDriver.h"
#include "LogNSS.h"
#include "NGSettings.h"
#include "NGVulkanBackend/Public/NGConvertVelocity.h"
#include "NSSHistory.h"
#include "NSSInclude.h"
#include "NSSModule.h"
#include "NSSProxy.h"
#include "PixelShaderUtils.h"
#include "PlanarReflectionSceneProxy.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceRayTracing.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryLayout.h"
#include "TranslucentRendering.h"

DECLARE_GPU_STAT(ArmNSSPass);

namespace
{
	FScreenPassTexture CopyAndCropIfNeeded(
		FRDGBuilder& GraphBuilder, const FScreenPassTexture& Texture, FIntPoint CroppedSize, const TCHAR* NameIfNeeded)
	{
		if (Texture.Texture->Desc.Extent == CroppedSize)
		{
			return Texture;
		}

		FRDGTextureDesc Desc = Texture.Texture->Desc;
		Desc.Extent = CroppedSize;

		// If the source texture is smaller than the requested crop region (e.g. SceneVelocity
		// held at 1×1 during early frames), AddCopyTexturePass would issue a vkCmdCopyImage that
		// overruns the source image bounds and triggers a Mali driver assertion. Return a
		// properly-sized zero texture so NSS still runs (keeping SDK state healthy) but receives
		// a black/zero buffer instead of crashing.
		if (Texture.Texture->Desc.Extent.X < CroppedSize.X || Texture.Texture->Desc.Extent.Y < CroppedSize.Y)
		{
			FRDGTextureDesc SafeDesc = Desc;
			SafeDesc.Flags |= TexCreate_UAV;
			FRDGTextureRef SafeTexture =
				GraphBuilder.CreateTexture(SafeDesc, NameIfNeeded, ERDGTextureFlags::MultiFrame);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SafeTexture), FLinearColor::Black);
			return FScreenPassTexture(SafeTexture, FIntRect(FIntPoint::ZeroValue, CroppedSize));
		}

		FRDGTextureRef CroppedTexture = GraphBuilder.CreateTexture(Desc, NameIfNeeded, ERDGTextureFlags::MultiFrame);

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.SourcePosition = FIntVector(Texture.ViewRect.Min.X, Texture.ViewRect.Min.Y, 0);
		CopyInfo.Size = FIntVector(CroppedSize.X, CroppedSize.Y, 0);
		CopyInfo.DestPosition = FIntVector(0, 0, 0);
		AddCopyTexturePass(GraphBuilder, Texture.Texture, CroppedTexture, CopyInfo);
		return FScreenPassTexture(CroppedTexture, FIntRect(FIntPoint::ZeroValue, CroppedSize));
	}

	static bool IsNssContextParamsChanged(
		const ffxApiCreateContextDescNss& CurrentParams, const ffxApiCreateContextDescNss& Params)
	{
		// clang-format off
		return (CurrentParams.maxRenderSize.width != Params.maxRenderSize.width)
			   || (CurrentParams.maxRenderSize.height != Params.maxRenderSize.height)
			   || (CurrentParams.maxUpscaleSize.width != Params.maxUpscaleSize.width)
			   || (CurrentParams.maxUpscaleSize.height != Params.maxUpscaleSize.height)
			   || (CurrentParams.flags != Params.flags)
			   || (CurrentParams.qualityMode != Params.qualityMode);
		// clang-format on
	}
}

//------------------------------------------------------------------------------------------------------
// To enforce quality modes we have to save the existing screen percentage so we can restore it later.
//------------------------------------------------------------------------------------------------------
float NSS::SavedScreenPercentage{100.0f};
static const TCHAR ScreenPercentageLOG[] =
	TEXT("ScreenPercentage should be in (0, 100) for NSS. Override r.ScreenPercentage to ideal 50");

struct NSSPass
{
	// clang-format off
	// Compute path: OutputTexture bound as UAV
	BEGIN_SHADER_PARAMETER_STRUCT(FComputeParameters, )
		RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(OutputTm1Texture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVMask)
		SHADER_PARAMETER(float, ExposureValue)
		RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVMask)
		RDG_TEXTURE_ACCESS(DebugViewsTexture, ERHIAccess::UAVMask)
	END_SHADER_PARAMETER_STRUCT()

	// Fragment path: OutputTexture bound as render target (RTV)
	BEGIN_SHADER_PARAMETER_STRUCT(FFragmentParameters, )
		RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(OutputTm1Texture, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVGraphics)
		SHADER_PARAMETER(float, ExposureValue)
		RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::RTV)
		RDG_TEXTURE_ACCESS(DebugViewsTexture, ERHIAccess::RTV)
	END_SHADER_PARAMETER_STRUCT()
	// clang-format on
};

//------------------------------------------------------------------------------------------------------
// NSS implementation.
//------------------------------------------------------------------------------------------------------
NSS::NSS() : Api(EFFXBackendAPI::Unknown), ApiAccessor(nullptr), CurrentGraphBuilder(nullptr), WrappedDenoiser(nullptr)
{
	FMemory::Memzero(PostInputs);

#if WITH_EDITOR
	bEnabledInEditor = true;
#endif

	FConsoleVariableDelegate EnabledChangedDelegate = FConsoleVariableDelegate::CreateStatic(&NSS::OnChangeNSSEnable);
	CVarEnableNSS->SetOnChangedCallback(EnabledChangedDelegate);

	FConsoleVariableDelegate ScreenPercentageChangedDelegate =
		FConsoleVariableDelegate::CreateStatic(&NSS::OnChangeScreenPercentage);
	IConsoleVariable* ScreenPercentageVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	ScreenPercentageVar->SetOnChangedCallback(ScreenPercentageChangedDelegate);

	FConsoleVariableDelegate ShaderQualityModeChangedDelegate =
		FConsoleVariableDelegate::CreateStatic(&NSS::OnChangeNSSShaderQualityMode);
	CVarNSSShaderQualityMode->SetOnChangedCallback(ShaderQualityModeChangedDelegate);
	OnChangeNSSShaderQualityMode(CVarNSSShaderQualityMode.AsVariable());

	if (CVarEnableNSS->GetBool())
	{
		SaveScreenPercentage();
		UpdateScreenPercentage();
	}

	GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionStateInfos);
}

NSS::~NSS()
{
	FScopeLock Lock(&Mutex);
	AvailableStates.Empty();
}

const TCHAR* NSS::GetDebugName() const
{
	return NSSHistory::GetUpscalerName();
}

void NSS::ReleaseState(NSSStateRef State)
{
	FScopeLock Lock(&Mutex);
	if (!AvailableStates.Contains(State) && State)
	{
		AvailableStates.Add(State);
	}
}

void NSS::RequestHistoryReset() const
{
	FScopeLock Lock(&Mutex);
	bNeedsHistoryReset = true;
}

void NSS::DeferredCleanup(uint64 FrameNum) const
{
	// The DeferDeleteFrames should be greater than 0.
	int32 DeferDeleteFrames = FMath::Max(1, CVarNGDeferDelete.GetValueOnRenderThread());
	FScopeLock Lock(&Mutex);

	TSet<NSSStateRef> DisposeStates;
	for (auto& State : AvailableStates)
	{
		ffxApiCreateContextDescNss const& CurrentParams = State->Params;
		if (State->LastUsedFrame <= FrameNum && ((FrameNum - State->LastUsedFrame) > DeferDeleteFrames))
		{
			DisposeStates.Add(State);
		}
		else if ((State->LastUsedFrame + DeferDeleteFrames) < FrameNum)
		{
			DisposeStates.Add(State);
		}
	}
	for (auto& State : DisposeStates)
	{
		AvailableStates.Remove(State);
	}
}

void NSS::RunDeferredCleanup() const
{
	DeferredCleanup(GFrameCounterRenderThread);
}

#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
void NSS::OnNSSMessage(uint32 type, const char* message)
{
	if (type == FFX_MESSAGE_TYPE_ERROR)
	{
		UE_LOG(LogNSS, Error, TEXT("%s"), UTF8_TO_TCHAR(message));
	}
	else if (type == FFX_MESSAGE_TYPE_WARNING)
	{
		UE_LOG(LogNSS, Warning, TEXT("%s"), UTF8_TO_TCHAR(message));
	}
}
#endif

void NSS::SaveScreenPercentage()
{
	SavedScreenPercentage =
		IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ScreenPercentage"))->GetValueOnGameThread();
}

void NSS::UpdateScreenPercentage()
{
	// Note: The ScreenPercentage should be in the range (0.0f, 100.0f).
	static IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	float CurrentValue = ScreenPercentage->GetFloat();
	if (CurrentValue >= 100.0f || CurrentValue <= 0.0f)
	{
		ScreenPercentage->Set(50.0f, ECVF_SetByConsole);
		UE_LOG(LogNSS, Warning, TEXT("%s"), ScreenPercentageLOG);
	}
}

void NSS::RestoreScreenPercentage()
{
	static IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	ScreenPercentage->Set(SavedScreenPercentage, ECVF_SetByConsole);
}

void NSS::OnChangeNSSEnable(IConsoleVariable* Var)
{
	if (CVarEnableNSS.GetValueOnGameThread())
	{
		SaveScreenPercentage();
		UpdateScreenPercentage();
	}
	else
	{
		RestoreScreenPercentage();
		// On disable:
		//   vkQueueWaitIdle (via BlockUntilGPUIdle) lets the NX run to completion so
		//   end_async resets the internal state before preemption.
		//   The cost is acceptable because this path is triggered only by user console-var toggle.
		INSSModule* Module = FModuleManager::GetModulePtr<INSSModule>(TEXT("NSS"));
		if (Module)
		{
			NSS* NSSPtr = Module->GetNSSUpscaler();
			if (NSSPtr)
			{
				// clang-format off
				ENQUEUE_RENDER_COMMAND(NSSBlockAndRequestHistoryReset)(
					[NSSPtr](FRHICommandListImmediate& RHICmdList)
					{
						RHICmdList.BlockUntilGPUIdle();
						NSSPtr->RequestHistoryReset();
					});
				// clang-format on
			}
		}
	}
}

void NSS::OnChangeScreenPercentage(IConsoleVariable* Var)
{
	if (CVarEnableNSS.GetValueOnGameThread())
	{
		if (Var->GetFloat() <= 0.0f || Var->GetFloat() >= 100.0f)
		{
			UE_LOG(LogNSS, Warning, TEXT("%s"), ScreenPercentageLOG);
			Var->Set(50.0f, ECVF_SetByConsole);
		}
		else
		{
			SaveScreenPercentage();
		}
	}
}

void NSS::OnChangeNSSShaderQualityMode(IConsoleVariable* Var)
{
	if (!Var)
	{
		return;
	}

	const int32 ShaderQualityMode = Var->GetInt();
	if (ShaderQualityMode < static_cast<int32>(ENSSShaderQualityMode::Quality)
		|| ShaderQualityMode > static_cast<int32>(ENSSShaderQualityMode::Performance))
	{
		UE_LOG(LogNSS,
			Warning,
			TEXT("r.NSS.ShaderQualityMode=%d is out of range [0,2] (0=Quality, 1=Balanced, 2=Performance)."
				 " Override to default 1 (Balanced)."),
			ShaderQualityMode);
		Var->Set(static_cast<int32>(ENSSShaderQualityMode::Balanced), ECVF_SetByConsole);
	}
}

FRDGBuilder* NSS::GetGraphBuilder()
{
	return CurrentGraphBuilder;
}

bool InitCreateContext(INGSharedBackend* ApiAccessor)
{
	static bool bSuccess = false;
	static bool bInitialized = false;
	if (bInitialized)
	{
		return bSuccess;
	}

	ffxApiCreateContextDescNss Params;
	FMemory::Memzero(Params);
	Params.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_NSS;
	Params.flags = 0;
	Params.flags |= FFX_API_NSS_CONTEXT_FLAG_QUANTIZED; // Currently we only support quantized model.
	Params.flags |= bool(ERHIZBuffer::IsInverted) ? FFX_API_NSS_CONTEXT_FLAG_DEPTH_INVERTED : 0;
	Params.flags |= FFX_API_NSS_CONTEXT_FLAG_HIGH_DYNAMIC_RANGE | FFX_API_NSS_CONTEXT_FLAG_DEPTH_INFINITE;
#if !PLATFORM_WINDOWS
	Params.flags |= FFX_API_NSS_CONTEXT_FLAG_ALLOW_16BIT;
#endif
	// Mirror the quality mode from the CVar so the pre-flight check tests the actual shader variant.
	Params.qualityMode = static_cast<FfxApiNssShaderQualityMode>(CVarNSSShaderQualityMode.GetValueOnAnyThread());

	// Final resolution (upscaled)
	Params.maxUpscaleSize.height = 16;
	Params.maxUpscaleSize.width = 16;
	// Render resolution (downscaled)
	Params.maxRenderSize.height = 8;
	Params.maxRenderSize.width = 8;
#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
	// Register message callback
	Params.fpMessage = &NSS::OnNSSMessage;
#endif
	ffxContext Nss;
	const FfxErrorCode ErrorCode = ApiAccessor->ffxCreateContext(&Nss, &Params.header);
	bSuccess = (ErrorCode == FFX_OK);
	if (bSuccess)
	{
		ApiAccessor->ffxDestroyContext(&Nss);
	}
	bInitialized = true;
	return bSuccess;
}

void NSS::Initialize() const
{
	ApiAccessor = INGSharedBackend::GetApiAccessor(Api);
	if (!ApiAccessor)
	{
		Api = EFFXBackendAPI::Unsupported;
		FString RHIName = GDynamicRHI->GetName();
		UE_LOG(LogNSS, Error, TEXT("NSS Temporal Upscaler not supported by '%s' rhi"), *RHIName);
	}

	if (IsApiSupported())
	{
		if (!ApiAccessor->IsNeuralGraphicSupported())
		{
			// clang-format off
			UE_LOG(LogNSS,
				Error,
				TEXT("NSS initialization failed due to unsupported device capabilities. Disabling NSS."
					"\nPlease use Vulkan Configurator to enable the ML extensions for Vulkan, and then restart the game."
				));
			// clang-format on
			Api = EFFXBackendAPI::Unsupported;
		}
		else if (!InitCreateContext(ApiAccessor))
		{
			UE_LOG(LogNSS, Error, TEXT("NSS Temporal Upscaler failed to create context"));
			Api = EFFXBackendAPI::Unsupported;
		}

		if (!WrappedDenoiser)
		{
			// Wrap any existing denoiser API as we override this to be able to generate the reactive mask.
			WrappedDenoiser = GScreenSpaceDenoiser ? GScreenSpaceDenoiser : IScreenSpaceDenoiser::GetDefaultDenoiser();
		}
		check(WrappedDenoiser);
		GScreenSpaceDenoiser = this;
	}
}

namespace
{
	INSS::FOutputs BlankOutput(FRDGBuilder& GraphBuilder, const INSS::FInputs& Inputs)
	{
		INSS::FOutputs Outputs;

		FRDGTextureDesc OutputColorDesc = Inputs.SceneColor.Texture->Desc;
		OutputColorDesc.Extent = Inputs.OutputViewRect.Size();
		OutputColorDesc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Outputs.FullRes.Texture = GraphBuilder.CreateTexture(
			OutputColorDesc, TEXT("ArmNssDisabledOutputSceneColor"), ERDGTextureFlags::MultiFrame);
		Outputs.FullRes.ViewRect = Inputs.OutputViewRect;

		AddClearRenderTargetPass(GraphBuilder, Outputs.FullRes.Texture, FLinearColor(1.0f, 1.0f, 0.0f));

		Outputs.NewHistory = new NSSHistory(nullptr, nullptr);

		return Outputs;
	}
}

INSS::FOutputs NSS::AddPasses(FRDGBuilder& GraphBuilder, const NSSView& SceneView, const NSSPassInput& PassInputs) const
{
	const FViewInfo& View = (FViewInfo&)(SceneView);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	const FIntPoint InputExtents = View.ViewRect.Size();
	const FIntPoint OutputExtents = View.GetSecondaryViewRectSize();
	Initialize();

	static const IConsoleVariable* ScreenPercentageVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	const float ScreenPercentageValue = ScreenPercentageVar ? ScreenPercentageVar->GetFloat() : 100.f;
	const float UpscaleRatio = ScreenPercentageValue != 0.0 ? 100.f / ScreenPercentageValue : 1.0f;

	if (!IsApiSupported() || (View.PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale))
	{
		return BlankOutput(GraphBuilder, PassInputs);
	}

	RDG_GPU_STAT_SCOPE(GraphBuilder, ArmNSSPass);
	RDG_EVENT_SCOPE(GraphBuilder, "ArmNSSPass");
	CurrentGraphBuilder = &GraphBuilder;
	bool bHistoryValid = View.PrevViewInfo.TemporalAAHistory.IsValid() && View.ViewState && !View.bCameraCut;
	if (bNeedsHistoryReset)
	{
		bHistoryValid = false;
	}
	const bool CanWritePrevViewInfo = !View.bStatePrevViewInfoIsReadOnly && View.ViewState;
	const bool bRenderDebugViews = CVarNSSDebug.GetValueOnRenderThread() == 1;
	ITemporalUpscaler::FOutputs Outputs;

	NSSStateRef CurrentNSSState;
	TRefCountPtr<INSSCustomHistory> PrevCustomHistory = PassInputs.PrevHistory;
	if (PrevCustomHistory.IsValid() && (PrevCustomHistory->GetDebugName() != GetDebugName()))
	{
		PrevCustomHistory.SafeRelease();
	}
	NSSHistory* CustomHistory = static_cast<NSSHistory*>(PrevCustomHistory.GetReference());
	bool HasValidContext = CustomHistory && CustomHistory->GetState().IsValid();
	TRefCountPtr<NSSHistory> NewHistory;
	//--------------------------------------------------------------------------------------------------------------
	// Initialize the NSS Context
	//   If a context has never been created, or if significant features of the frame have changed since the current
	//   context was created, tear down any existing contexts and create a new one matching the current frame.
	//--------------------------------------------------------------------------------------------------------------
	const bool bUseFragmentShader = CVarNSSUseFragmentShader.GetValueOnAnyThread() != 0;
	{
		ffxApiCreateContextDescNss Params;
		FMemory::Memzero(Params);
		Params.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_NSS;
		//----------------------------------------------------------------------------------------------------------
		// Describe the Current Frame
		//   Collect the features of the current frame and the current NSS history,
		//   so we can make decisions about whether any existing NSS context is currently usable.
		//----------------------------------------------------------------------------------------------------------
		{
			Params.flags = 0;
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_QUANTIZED; // Currently we only support quantized model.
			Params.flags |= bool(ERHIZBuffer::IsInverted) ? FFX_API_NSS_CONTEXT_FLAG_DEPTH_INVERTED : 0;
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_HIGH_DYNAMIC_RANGE | FFX_API_NSS_CONTEXT_FLAG_DEPTH_INFINITE;
#if !PLATFORM_WINDOWS
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_ALLOW_16BIT;
#endif
			if (bUseFragmentShader)
			{
				Params.flags |= FFX_API_NSS_CONTEXT_FLAG_PRE_PROCESS_FRAGMENT;
				Params.flags |= FFX_API_NSS_CONTEXT_FLAG_POST_PROCESS_FRAGMENT;
			}
			Params.qualityMode =
				static_cast<FfxApiNssShaderQualityMode>(CVarNSSShaderQualityMode.GetValueOnAnyThread());

			// Final resolution (upscaled)
			Params.maxUpscaleSize.height = OutputExtents.Y;
			Params.maxUpscaleSize.width = OutputExtents.X;
			// Render resolution (downscaled)
			Params.maxRenderSize.height = InputExtents.Y;
			Params.maxRenderSize.width = InputExtents.X;
#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
			// Register message callback
			Params.fpMessage = &NSS::OnNSSMessage;
#endif
		}
		// We want to reuse NSS states rather than recreating them wherever possible as they allocate significant
		// memory for their internal resources.
		// The current custom history is the ideal, but the recently released states can be reused with a simple
		// reset too when the engine cuts the history. This reduces the memory churn imposed by camera cuts.
		if (HasValidContext)
		{
			ffxApiCreateContextDescNss const& CurrentParams = CustomHistory->GetState()->Params;
			if (IsNssContextParamsChanged(CurrentParams, Params))
			{
				// Params changed — the existing session cannot be reused; let it be recreated.
				HasValidContext = false;
			}
			else
			{
				CurrentNSSState = CustomHistory->GetState();
				if (View.ViewState && CurrentNSSState->ViewID != View.ViewState->UniqueID)
				{
					// CustomHistory belongs to a different view — do not steal it; fall through
					// to AvailableStates so each view keeps its own context.
					HasValidContext = false;
				}
				else if (CurrentNSSState->LastUsedFrame == GFrameCounterRenderThread)
				{
					// Context already consumed this frame (e.g. stereo rendering).
					// Reuse the session but clear temporal history to avoid ghosting artifacts.
					bHistoryValid = false;
				}
				if (HasValidContext)
				{
					CurrentNSSState->LastUsedFrame = GFrameCounterRenderThread;
					CurrentNSSState->ViewID = View.ViewState->UniqueID;
				}
			}
		}
		if (!HasValidContext)
		{
			FScopeLock Lock(&Mutex);
			TSet<NSSStateRef> DisposeStates;
			for (auto& State : AvailableStates)
			{
				ffxApiCreateContextDescNss const& CurrentParams = State->Params;
				if ((State->LastUsedFrame == GFrameCounterRenderThread)
					|| (View.ViewState && State->ViewID != View.ViewState->UniqueID))
				{
					// These states can't be reused immediately but perhaps a future frame,
					// otherwise we break split screen.
					continue;
				}
				else if (IsNssContextParamsChanged(CurrentParams, Params))
				{
					// Params changed — this state can never be reused in this session; remove from pool.
					DisposeStates.Add(State);
				}
				else
				{
					CurrentNSSState = State;
					// Claim this state inside the lock by stamping LastUsedFrame and
					// ViewID now.  Any concurrent AddPasses call that acquires this same
					// mutex will see LastUsedFrame == GFrameCounterRenderThread and skip
					// the state, preventing double-claim without having to remove it from
					// the pool and add it back.
					CurrentNSSState->LastUsedFrame = GFrameCounterRenderThread;
					CurrentNSSState->ViewID = View.ViewState->UniqueID;
					HasValidContext = true;
					bHistoryValid = false;
					break;
				}
			}
			for (auto& State : DisposeStates)
			{
				AvailableStates.Remove(State);
			}
		}
		if (!HasValidContext)
		{
			// For a new context, allocate the necessary scratch memory for the chosen backend
			CurrentNSSState = new NSSState(ApiAccessor);
			check(CurrentNSSState);
			CurrentNSSState->LastUsedFrame = GFrameCounterRenderThread;
			CurrentNSSState->ViewID = View.ViewState->UniqueID;
		}

		// Keep the current state in AvailableStates so the scan on the next frame can find and
		// reuse it even when UE does not pass PrevHistory back (e.g. bStatePrevViewInfoIsReadOnly).
		// This prevents two concurrent ~3.5 MB Data Graph sessions from existing simultaneously.
		{
			FScopeLock Lock(&Mutex);
			if (!AvailableStates.Contains(CurrentNSSState))
			{
				AvailableStates.Add(CurrentNSSState);
			}
		}
		//----------------------------------------------------------------------------------------------------------
		// Update History Data (Part 1)
		//   Prepare the view to receive this frame's history data.
		//   This must be done before any attempt to re-create an NSS context, if that's needed.
		//----------------------------------------------------------------------------------------------------------
		if (CanWritePrevViewInfo)
		{
			// Releases the existing history texture inside the wrapper object,
			// this doesn't release NewHistory itself
			View.ViewState->PrevFrameViewInfo.TemporalAAHistory.SafeRelease();
			View.ViewState->PrevFrameViewInfo.TemporalAAHistory.ViewportRect =
				FIntRect(0, 0, OutputExtents.X, OutputExtents.Y);
			View.ViewState->PrevFrameViewInfo.TemporalAAHistory.ReferenceBufferSize = OutputExtents;
		}
		NewHistory = new NSSHistory(CurrentNSSState, const_cast<NSS*>(this));
		check(NewHistory);

		//----------------------------------------------------------------------------------------------------------
		// Invalidate NSS Contexts
		//   If a context already exists but it is not valid for the current frame's features, clean it up in
		//   preparation for creating a new one.
		//----------------------------------------------------------------------------------------------------------
		if (HasValidContext)
		{
			ffxApiCreateContextDescNss const& CurrentParams = CurrentNSSState->Params;
			// Display size must match for splitscreen to work.
			if (IsNssContextParamsChanged(CurrentParams, Params))
			{
				ApiAccessor->ffxDestroyContext(&CurrentNSSState->Nss);
				HasValidContext = false;
				bHistoryValid = false;
			}
		}
		//------------------------------------------------------
		// Create NSS Contexts
		//   If no valid context currently exists, create one.
		//------------------------------------------------------
		if (!HasValidContext)
		{
			FfxErrorCode ErrorCode = ApiAccessor->ffxCreateContext(&CurrentNSSState->Nss, &Params.header);
			check(ErrorCode == FFX_OK);
			if (ErrorCode != FFX_OK)
			{
				{
					FScopeLock Lock(&Mutex);
					AvailableStates.Remove(CurrentNSSState);
				}
				CurrentNSSState.SafeRelease();
				return BlankOutput(GraphBuilder, PassInputs);
			}
			HasValidContext = true;
			bHistoryValid = false;
			FMemory::Memcpy(CurrentNSSState->Params, Params);
		}
	}
	//--------------------------------------------------------------------------------------------------------------
	// Organize Inputs (Part 1)
	//   Some inputs NSS requires are available now, but will no longer be directly available once we get inside
	//   the RenderGraph.  Go ahead and collect the ones we can.
	//--------------------------------------------------------------------------------------------------------------
	ffxApiDispatchDescNss NssDispatchParams = {};
	{
		NssDispatchParams.header.type = FFX_API_DISPATCH_DESC_TYPE_NSS;
		NssDispatchParams.flags = 0;
		NssDispatchParams.flags |= bRenderDebugViews ? FFX_API_NSS_DISPATCH_FLAG_DRAW_DEBUG_VIEW : 0;
		// Whether to abandon the history in the state on camera cuts
		NssDispatchParams.reset = !bHistoryValid;
		NssDispatchParams.frameTimeDelta = View.Family->Time.GetDeltaWorldTimeSeconds() * 1000.f;
		// Reference shaders use subtraction of jitter and it's in input resolution units, instead of UV units.
		NssDispatchParams.jitterOffset.x = -PassInputs.TemporalJitterPixels.X;
		NssDispatchParams.jitterOffset.y = -PassInputs.TemporalJitterPixels.Y;
		NssDispatchParams.renderSize.width = InputExtents.X;
		NssDispatchParams.renderSize.height = InputExtents.Y;
		NssDispatchParams.upscaleSize.width = OutputExtents.X;
		NssDispatchParams.upscaleSize.height = OutputExtents.Y;
		// Parameters for motion vectors:
		NssDispatchParams.motionVectorScale.x = InputExtents.X;
		NssDispatchParams.motionVectorScale.y = InputExtents.Y;
		NssDispatchParams.cameraFovAngleVertical = View.ViewMatrices.ComputeHalfFieldOfViewPerAxis().Y * 2.0f;
		if (bool(ERHIZBuffer::IsInverted))
		{
			NssDispatchParams.cameraNear = FLT_MAX;
			NssDispatchParams.cameraFar = View.ViewMatrices.ComputeNearPlane();
		}
		else
		{
			NssDispatchParams.cameraNear = View.ViewMatrices.ComputeNearPlane();
			NssDispatchParams.cameraFar = FLT_MAX;
		}
	}
	//------------------------------
	// Add NSS to the RenderGraph
	//------------------------------
	FRDGTextureDesc UpscaledOutputColorDesc = PassInputs.SceneColor.Texture->Desc;
	UpscaledOutputColorDesc.Extent = OutputExtents;
	// clang-format off
	UpscaledOutputColorDesc.Flags = bUseFragmentShader ?
		(TexCreate_ShaderResource | TexCreate_RenderTargetable) : (TexCreate_ShaderResource | TexCreate_UAV);
	// clang-format on
	UpscaledOutputColorDesc.Format = EPixelFormat::PF_FloatR11G11B10;
	FRDGTextureRef UpscaledOutputColor = GraphBuilder.CreateTexture(
		UpscaledOutputColorDesc, TEXT("ArmNSSUpscaledOutputSceneColor"), ERDGTextureFlags::MultiFrame);
	// If SceneColor or SceneDepth is still at 1×1 (UE has not yet produced a full-resolution
	// render target), skip the NSS dispatch entirely to avoid an invalid copy in the DDK.
	// SceneVelocity may legitimately be 1×1 when there is no motion in the scene; that case
	// is handled safely inside CopyAndCropIfNeeded below.
	if (PassInputs.SceneColor.Texture->Desc.Extent.X < InputExtents.X
		|| PassInputs.SceneColor.Texture->Desc.Extent.Y < InputExtents.Y
		|| PassInputs.SceneDepth.Texture->Desc.Extent.X < InputExtents.X
		|| PassInputs.SceneDepth.Texture->Desc.Extent.Y < InputExtents.Y)
	{
		return BlankOutput(GraphBuilder, PassInputs);
	}

	FScreenPassTexture InputColor =
		CopyAndCropIfNeeded(GraphBuilder, PassInputs.SceneColor, InputExtents, TEXT("InputColorTexture"));
	FScreenPassTexture InputDepth =
		CopyAndCropIfNeeded(GraphBuilder, PassInputs.SceneDepth, InputExtents, TEXT("InputDepthTexture"));
	FScreenPassTexture InputVelocity =
		CopyAndCropIfNeeded(GraphBuilder, PassInputs.SceneVelocity, InputExtents, TEXT("InputVelocityTexture"));
	FRDGTextureRef DebugViews{};
	if (bRenderDebugViews)
	{
		FRDGTextureDesc DebugViewsDesc = UpscaledOutputColorDesc;
#if PLATFORM_CPU_X86_FAMILY
		// Currently tensor read with fragment job is forbidden in emulation layer, will fall back to compute.
		// This can be removed once the fragment shader supports reading tensors in emulation layer.
		DebugViewsDesc.Flags |= TexCreate_UAV;
#endif
		DebugViews = GraphBuilder.CreateTexture(DebugViewsDesc, TEXT("ArmNSSDebugViews"), ERDGTextureFlags::MultiFrame);
	}
	auto* ApiAccess = ApiAccessor;
	auto CurrentApi = Api;
	if (CurrentApi == EFFXBackendAPI::Vulkan)
	{
		//------------------------------------------------------------------------------------------------------
		// Consolidate Motion Vectors
		//   UE4 motion vectors are in sparse format by default.  Convert them to a format consumable by NSS.
		//------------------------------------------------------------------------------------------------------
		if (!IsValidRef(MotionVectorRT) || MotionVectorRT->GetDesc().Extent.X != InputExtents.X
			|| MotionVectorRT->GetDesc().Extent.Y != InputExtents.Y)
		{
			ETextureCreateFlags DescFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable;
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(InputExtents,
				PF_G16R16F,
				FClearValueBinding::Transparent,
				DescFlags,
				TexCreate_ShaderResource | TexCreate_RenderTargetable,
				false));
			GRenderTargetPool.FindFreeElement(
				GraphBuilder.RHICmdList, Desc, MotionVectorRT, TEXT("NSSMotionVectorTexture"));
			if (!IsValidRef(MotionVectorRT))
			{
				return BlankOutput(GraphBuilder, PassInputs);
			}
		}
		FRDGTextureRef MotionVectorTexture = GraphBuilder.RegisterExternalTexture(MotionVectorRT);
		helper_NGConvertVelocity(GraphBuilder,
			MotionVectorTexture,
			InputDepth.Texture,
			InputVelocity.Texture,
			PassInputs.SceneVelocity.ViewRect,
			&View);

		auto DoAddPass = [&](auto* PassParameters)
		{
			PassParameters->ColorTexture = InputColor.Texture;
			PassParameters->DepthTexture = InputDepth.Texture;
			PassParameters->VelocityTexture = MotionVectorTexture;
			PassParameters->ExposureValue = View.PreExposure;
			PassParameters->OutputTexture = UpscaledOutputColor;
			// clang-format off
			PassParameters->OutputTm1Texture = (CustomHistory && CustomHistory->UpscaledColour.IsValid())
				? GraphBuilder.RegisterExternalTexture(CustomHistory->UpscaledColour)
				: GSystemTextures.GetBlackDummy(GraphBuilder);
			PassParameters->DebugViewsTexture = bRenderDebugViews ? DebugViews : nullptr;

			const ERDGPassFlags PassFlags = bUseFragmentShader
				? (ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass)
				: (ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass);
			GraphBuilder.AddPass(RDG_EVENT_NAME("NG NSS (VK backend)"),
				PassParameters,
				PassFlags,
				[&View, &PassInputs, CurrentApi, ApiAccess, PassParameters, NssDispatchParams, CurrentNSSState, bUseFragmentShader](
					FRHICommandListImmediate& RHICmdList)
				{
					const FfxApiResourceState shaderReadState =
						bUseFragmentShader ? FFX_API_RESOURCE_STATE_PIXEL_READ : FFX_API_RESOURCE_STATE_COMPUTE_READ;
#if PLATFORM_CPU_X86_FAMILY
					// Currently tensor read with fragment job is forbidden in emulation layer, will fall back to compute.
					// This can be removed once the fragment shader supports reading tensors in emulation layer.
					const FfxApiResourceState shaderWriteState = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
#else
					const FfxApiResourceState shaderWriteState =
						bUseFragmentShader ? FFX_API_RESOURCE_STATE_RENDER_TARGET : FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
#endif
					ffxApiDispatchDescNss DispatchParams = NssDispatchParams;
					DispatchParams.color = ApiAccess->GetNativeResource(PassParameters->ColorTexture->GetRHI(), shaderReadState);
					DispatchParams.depth = ApiAccess->GetNativeResource(PassParameters->DepthTexture->GetRHI(), shaderReadState);
					DispatchParams.motionVectors = ApiAccess->GetNativeResource(PassParameters->VelocityTexture->GetRHI(), shaderReadState);
					DispatchParams.output = ApiAccess->GetNativeResource(PassParameters->OutputTexture->GetRHI(), shaderWriteState);
					DispatchParams.outputTm1 = ApiAccess->GetNativeResource(PassParameters->OutputTm1Texture->GetRHI(), shaderReadState);
					DispatchParams.exposure = PassParameters->ExposureValue;
					PassParameters->ColorTexture->MarkResourceAsUsed();
					PassParameters->DepthTexture->MarkResourceAsUsed();
					PassParameters->VelocityTexture->MarkResourceAsUsed();
					PassParameters->OutputTexture->MarkResourceAsUsed();
					PassParameters->OutputTm1Texture->MarkResourceAsUsed();
					if (PassParameters->DebugViewsTexture)
					{
						DispatchParams.debugViews = ApiAccess->GetNativeResource(
							PassParameters->DebugViewsTexture->GetRHI(), shaderWriteState);
						PassParameters->DebugViewsTexture->MarkResourceAsUsed();
					}

					ApiAccess->ForceUAVTransition(
						RHICmdList, PassParameters->OutputTexture->GetRHI(), bUseFragmentShader ? ERHIAccess::RTV : ERHIAccess::UAVMask);

					RHICmdList.EnqueueLambda(
						[ApiAccess, CurrentNSSState, DispatchParams](FRHICommandListImmediate& cmd) mutable
						{
							DispatchParams.commandList = ApiAccess->GetNativeCommandBuffer(cmd, nullptr);
							const auto Code = ApiAccess->ffxDispatch(&CurrentNSSState->Nss, &DispatchParams.header);
							check(Code == FFX_OK);
						});
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				});
			}; // End of AddPass lambda
		// clang-format on
		if (bUseFragmentShader)
		{
			DoAddPass(GraphBuilder.AllocParameters<NSSPass::FFragmentParameters>());
		}
		else
		{
			DoAddPass(GraphBuilder.AllocParameters<NSSPass::FComputeParameters>());
		}
	}

	// Output Final Colour
	if (bRenderDebugViews)
	{
		// Output Debug Views
		Outputs.FullRes = FScreenPassTexture(DebugViews);
	}
	else
	{
		// Output Final Colour
		Outputs.FullRes = FScreenPassTexture(UpscaledOutputColor);
	}

	//--------------------------------------------------------------------------------------------------------------
	// Update History Data (Part 2)
	//   Extract the output produced by the NSS Dispatch into the history reference we prepared to receive that
	//   output during Part 1.
	//--------------------------------------------------------------------------------------------------------------
	if (CanWritePrevViewInfo)
	{
		// Check 'CanWritePrevViewInfo' before QueueTextureExtraction. Avoid extracting textures while paused,
		// keeping behavior consistent with engine resources like TemporalAA that skip history
		// updates such as when the world is paused.
		GraphBuilder.QueueTextureExtraction(
			UpscaledOutputColor, &View.ViewState->PrevFrameViewInfo.TemporalAAHistory.RT[0]);
		GraphBuilder.QueueTextureExtraction(UpscaledOutputColor, &NewHistory->UpscaledColour);
	}
	Outputs.NewHistory = NewHistory;
	return Outputs;
}

INSS* NSS::Fork_GameThread(const class FSceneViewFamily& InViewFamily) const
{
	Initialize();

	INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));

	return new NSSProxy(NSSModuleInterface.GetNSSUpscaler());
}

float NSS::GetMinUpsampleResolutionFraction() const
{
	if (IsApiSupported())
	{
		return 0.25f;
	}
	else
	{
		return 0.0f;
	}
}

float NSS::GetMaxUpsampleResolutionFraction() const
{
	if (IsApiSupported())
	{
		return 1.0f;
	}
	else
	{
		return 0.0f;
	}
}

void NSS::SetPostProcessingInputs(FPostProcessingInputs const& NewInputs)
{
	PostInputs = NewInputs;
}

//-------------------------------------------------------------------------------------
// As the upscaler retains some resources during the frame they must be released here to avoid leaking or accessing
// dangling pointers.
//-------------------------------------------------------------------------------------
void NSS::EndOfFrame()
{
	PostInputs.SceneTextures = nullptr;
	bNeedsHistoryReset = false;
	RunDeferredCleanup();
#if WITH_EDITOR
	bEnabledInEditor = true;
#endif
}

//-------------------------------------------------------------------------------------
// Updates the state of dynamic resolution for this frame.
//-------------------------------------------------------------------------------------
void NSS::UpdateDynamicResolutionState()
{
	GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionStateInfos);
}

//-------------------------------------------------------------------------------------
// In the Editor it is necessary to disable the view extension via the upscaler API so it doesn't cause conflicts.
//-------------------------------------------------------------------------------------
#if WITH_EDITOR
bool NSS::IsEnabledInEditor() const
{
	return bEnabledInEditor;
}

void NSS::SetEnabledInEditor(bool bEnabled)
{
	bEnabledInEditor = bEnabled;
}
#endif

//----------------------------------------------------------------------------------------------------
// The interesting function in the denoiser API that lets us capture the reflections texture.
// This has to replicate the behavior of the engine, only we retain a reference to the output texture.
//----------------------------------------------------------------------------------------------------
IScreenSpaceDenoiser::FReflectionsOutputs NSS::DenoiseReflections(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return WrappedDenoiser->DenoiseReflections(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

//-------------------------------------------------------------------------------------
// The remaining denoiser API simply passes through to the wrapped denoiser.
//-------------------------------------------------------------------------------------
IScreenSpaceDenoiser::EShadowRequirements NSS::GetShadowRequirements(
	const FViewInfo& View, const FLightSceneInfo& LightSceneInfo, const FShadowRayTracingConfig& RayTracingConfig) const
{
	return WrappedDenoiser->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
}

void NSS::DenoiseShadowVisibilityMasks(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const TStaticArray<FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize>& InputParameters,
	const int32 InputParameterCount,
	TStaticArray<FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize>& Outputs) const
{
	return WrappedDenoiser->DenoiseShadowVisibilityMasks(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, InputParameters, InputParameterCount, Outputs);
}

IScreenSpaceDenoiser::FPolychromaticPenumbraOutputs NSS::DenoisePolychromaticPenumbraHarmonics(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FPolychromaticPenumbraHarmonics& Inputs) const
{
	return WrappedDenoiser->DenoisePolychromaticPenumbraHarmonics(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs);
}

IScreenSpaceDenoiser::FReflectionsOutputs NSS::DenoiseWaterReflections(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return WrappedDenoiser->DenoiseWaterReflections(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

IScreenSpaceDenoiser::FAmbientOcclusionOutputs NSS::DenoiseAmbientOcclusion(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FAmbientOcclusionInputs& ReflectionInputs,
	const FAmbientOcclusionRayTracingConfig RayTracingConfig) const
{
	return WrappedDenoiser->DenoiseAmbientOcclusion(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

FSSDSignalTextures NSS::DenoiseDiffuseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseDiffuseIndirect(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

#if ENGINE_HAS_DENOISE_INDIRECT
FSSDSignalTextures NSS::DenoiseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	// This code path doesn't denoise indirect specular. It should not be hit at all.
	check(0);

	FSSDSignalTextures DummyReturn;
	return DummyReturn;
}
#endif

IScreenSpaceDenoiser::FDiffuseIndirectOutputs NSS::DenoiseSkyLight(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

FSSDSignalTextures NSS::DenoiseDiffuseIndirectHarmonic(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectHarmonic& Inputs,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters) const
{
	return WrappedDenoiser->DenoiseDiffuseIndirectHarmonic(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, CommonDiffuseParameters);
}

bool NSS::SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const
{
	return WrappedDenoiser->SupportsScreenSpaceDiffuseIndirectDenoiser(Platform);
}

FSSDSignalTextures NSS::DenoiseScreenSpaceDiffuseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseScreenSpaceDiffuseIndirect(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
