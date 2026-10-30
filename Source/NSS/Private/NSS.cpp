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

#include "NSS.h"

#include "HAL/IConsoleManager.h"
#include "LegacyScreenPercentageDriver.h"
#include "LogNSS.h"
#include "NGSettings.h"
#include "NSSHistory.h"
#include "NSSInclude.h"
#include "NSSModule.h"
#include "PixelShaderUtils.h"
#include "PlanarReflectionSceneProxy.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceRayTracing.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryLayout.h"
#include "TranslucentRendering.h"

#define GFrameCounterRenderThread GFrameNumberRenderThread

DECLARE_GPU_STAT(ArmNSSPass);

namespace
{
	FScreenPassTexture CopyAndCropIfNeeded(FRDGBuilder& GraphBuilder,
		const FScreenPassTexture& Texture,
		FIntPoint PaddingOnOutput,
		const TCHAR* NameIfNeeded)
	{
		if (PaddingOnOutput == FIntPoint::ZeroValue)
		{
			return Texture;
		}

		FIntPoint CroppedSize = Texture.ViewRect.Size() - PaddingOnOutput;
		FRDGTextureDesc Desc = Texture.Texture->Desc;
		QuantizeSceneBufferSize(CroppedSize, Desc.Extent);
		Desc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
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
		return (CurrentParams.maxRenderSize.width != Params.maxRenderSize.width)
			   || (CurrentParams.maxRenderSize.height != Params.maxRenderSize.height)
			   || (CurrentParams.maxUpscaleSize.width != Params.maxUpscaleSize.width)
			   || (CurrentParams.maxUpscaleSize.height != Params.maxUpscaleSize.height)
			   || (CurrentParams.flags != Params.flags);
	}
}

//------------------------------------------------------------------------------------------------------
// To enforce quality modes we have to save the existing screen percentage so we can restore it later.
//------------------------------------------------------------------------------------------------------
float NSS::SavedScreenPercentage{100.0f};
static const TCHAR ScreenPercentageLOG[] =
	TEXT("ScreenPercentage should be in (0, 100) for NSS. Override r.ScreenPercentage to ideal 50");

IMPLEMENT_GLOBAL_SHADER(FNssConvertVelocity, "/Plugin/NSS/Private/NssConvertVelocityPS.usf", "main", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FNssMirrorPadPS, "/Plugin/NSS/Private/NssMirrorPad.usf", "MirrorPadPS", SF_Pixel);

struct NSSPass
{
	// clang-format off
	BEGIN_SHADER_PARAMETER_STRUCT (FParameters, )
		RDG_TEXTURE_ACCESS(ColorTexture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(OutputTm1Texture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(DepthTm1Texture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVMask)
		RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVMask)
		RDG_TEXTURE_ACCESS(DebugViewsTexture, ERHIAccess::UAVMask)
		SHADER_PARAMETER(float, ExposureValue)
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

	if (CVarEnableNSS->GetBool())
	{
		SaveScreenPercentage();
		UpdateScreenPercentage();
	}

	GEngine->GetDynamicResolutionCurrentStateInfos(DynamicResolutionStateInfos);
}

NSS::~NSS()
{
	DeferredCleanup(0);
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

void NSS::DeferredCleanup(uint64 FrameNum) const
{
	FScopeLock Lock(&Mutex);
	AvailableStates.Empty();
}

INGSharedBackend* NSS::GetApiAccessor(EFFXBackendAPI& Api)
{
	INGSharedBackend* ApiAccessor = nullptr;
	FString RHIName = GDynamicRHI->GetName();
	INGSharedBackendModule* VkBackend = FModuleManager::GetModulePtr<INGSharedBackendModule>(TEXT("NGVulkanBackend"));

	if (IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::ES3_1) && RHIName == TEXT("Vulkan")
		&& VkBackend)
	{
		ApiAccessor = VkBackend->GetBackend();
		if (ApiAccessor)
		{
			Api = EFFXBackendAPI::Vulkan;
			return ApiAccessor;
		}
	}

	return ApiAccessor;
}

#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
void NSS::OnNSSMessage(uint32 type, const wchar_t* message)
{
	if (type == FFX_MESSAGE_TYPE_ERROR)
	{
		UE_LOG(LogNSS, Error, TEXT("%s"), WCHAR_TO_TCHAR(message));
	}
	else if (type == FFX_MESSAGE_TYPE_WARNING)
	{
		UE_LOG(LogNSS, Warning, TEXT("%s"), WCHAR_TO_TCHAR(message));
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

	// Final resolution (upscaled)
	Params.maxUpscaleSize.height = 16;
	Params.maxUpscaleSize.width = 16;
	// Render resolution (downscaled)
	Params.maxRenderSize.height = 8;
	Params.maxRenderSize.width = 8;
#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
	Params.flags |= FFX_API_NSS_CONTEXT_FLAG_ENABLE_DEBUG_CHECKING;
	// Register message callback
	Params.fpMessage = &NSS::OnNSSMessage;
#endif

	ffxContext Nss;
	bSuccess = ApiAccessor->ffxCreateContext(&Nss, &Params.header) == FFX_OK;
	if (bSuccess)
	{
		ApiAccessor->ffxDestroyContext(&Nss);
	}
	bInitialized = true;
	return bSuccess;
}

void NSS::Initialize() const
{
	ApiAccessor = GetApiAccessor(Api);
	if (!ApiAccessor)
	{
		Api = EFFXBackendAPI::Unsupported;
		FString RHIName = GDynamicRHI->GetName();
		UE_LOG(LogNSS, Error, TEXT("NSS Temporal Upscaler not supported by '%s' rhi"), *RHIName);
	}

	if (IsApiSupported())
	{
		if (!InitCreateContext(ApiAccessor))
		{
			if (!ApiAccessor->IsNeuralGraphicSupported())
			{
				// clang-format off
				UE_LOG(LogNSS,
					Error,
					TEXT("NSS initialization failed due to unsupported device capabilities. Disabling NSS."
						"\nPlease use Vulkan Configurator to enable Vulkan ML Emulation layers, and then restart the game."
				));
				// clang-format on
			}
			else
			{
				UE_LOG(LogNSS, Error, TEXT("NSS Temporal Upscaler failed to create context"));
			}
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
	FIntPoint RoundUpToMultiple(FIntPoint in, uint32 multiple)
	{
		return ((in + multiple - 1) / multiple) * multiple;
	}

	FScreenPassTexture BlankOutput(FRDGBuilder& GraphBuilder, const NSSPassInput& Inputs, FIntPoint OutputExtents)
	{
		FScreenPassTexture OutputsFullRes;

		FRDGTextureDesc OutputColorDesc = Inputs.SceneColorTexture->Desc;
		OutputColorDesc.Extent = OutputExtents;
		OutputColorDesc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		OutputsFullRes.Texture = GraphBuilder.CreateTexture(
			OutputColorDesc, TEXT("ArmNssDisabledOutputSceneColor"), ERDGTextureFlags::MultiFrame);
		OutputsFullRes.ViewRect = FIntRect(FIntPoint::ZeroValue, OutputExtents);

		AddClearRenderTargetPass(GraphBuilder, OutputsFullRes.Texture, FLinearColor(1.0f, 1.0f, 0.0f));

		return OutputsFullRes;
	}
}

void NSS::AddPasses(FRDGBuilder& GraphBuilder,
	const NSSView& SceneView,
	const NSSPassInput& PassInputs,
	FRDGTextureRef* OutSceneColorTexture,
	FIntRect* OutSceneColorViewRect,
	FRDGTextureRef* OutSceneColorHalfResTexture,
	FIntRect* OutSceneColorHalfResViewRect) const
{
	if (!OutSceneColorTexture || !OutSceneColorViewRect || !OutSceneColorHalfResTexture
		|| !OutSceneColorHalfResViewRect)
	{
		return;
	}

	const NSSView& View = SceneView;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	FIntPoint InputExtents = View.ViewRect.Size();
	FIntPoint OutputExtents = View.GetSecondaryViewRectSize();
	Initialize();

	static const IConsoleVariable* ScreenPercentageVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	const float ScreenPercentageValue = ScreenPercentageVar ? ScreenPercentageVar->GetFloat() : 100.f;
	const float UpscaleRatio = ScreenPercentageValue != 0.0 ? 100.f / ScreenPercentageValue : 1.0f;

	// The API must be supported, the underlying code has to handle downscaling as well as upscaling.
	check(IsApiSupported() && (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale));

	RDG_GPU_STAT_SCOPE(GraphBuilder, ArmNSSPass);
	RDG_EVENT_SCOPE(GraphBuilder, "ArmNSSPass");
	CurrentGraphBuilder = &GraphBuilder;
	bool bHistoryValid = View.PrevViewInfo.TemporalAAHistory.IsValid() && View.ViewState && !View.bCameraCut;
	const bool CanWritePrevViewInfo = !View.bStatePrevViewInfoIsReadOnly && View.ViewState;
	const bool bRenderDebugViews = CVarNSSDebug.GetValueOnRenderThread() == 1;
	FRDGTextureRef SceneColor = PassInputs.SceneColorTexture;
	FRDGTextureRef SceneDepth = PassInputs.SceneDepthTexture;
	FRDGTextureRef SceneVelocity = PassInputs.SceneVelocityTexture;
	FScreenPassTexture PaddedInputColor(SceneColor);
	FScreenPassTexture PaddedInputDepth(SceneDepth);
	FScreenPassTexture PaddedInputVelocity(SceneVelocity);
	// Note that the texture extent might be LARGER than the ViewRect, as in the editor it won't shrink the render
	// target if the viewport is shrunk (as an optimisation presumably).
	// The network requires the inputs to be a multiple of 8 in both width and height (i.e. a 540p input frame
	// should be padded to 544) with mirroring if padding is required. The padding is always done on the
	// bottom/right. The post-processing also produces a padded output, with the padding amount upscaled from the
	// input padding amount. Unfortunately the calculation for the output size is more complex. Since the input of
	// the network can be padded by up to 7 pixels the output will be cropped up to 7 times the ratio of input
	// resolution to output resolution e.g. input res 545 upscaled with a ratio of 2 to 1090
	//      545 pads to 552.
	//      552 * 2 =  1104
	//      1104 - 1090 = 14
	FIntPoint PaddedInputSize = RoundUpToMultiple(InputExtents, 8);
	FIntPoint PaddingOnInput = PaddedInputSize - InputExtents;
	FVector2D ScaledPaddedInputSizeF = FVector2D(PaddedInputSize) * UpscaleRatio;
	FIntPoint PaddedOutputSize =
		FIntPoint(FMath::RoundToInt(ScaledPaddedInputSizeF.X), FMath::RoundToInt(ScaledPaddedInputSizeF.Y));
	FIntPoint PaddingOnOutput = PaddedOutputSize - OutputExtents;
	// Copy the input scene color, depth and velocity textures and add padding around the edges if necessary
	FScreenPassTexture ScreenPassSceneColor(SceneColor);
	FScreenPassTexture ScreenPassSceneDepth(SceneDepth);
	FScreenPassTexture ScreenPassSceneVelocity(SceneVelocity);
	if (PaddingOnInput != FIntPoint::ZeroValue)
	{
		FRDGTextureDesc ColorPaddedDesc = SceneColor->Desc;
		ColorPaddedDesc.Extent = PaddedInputSize;
		ColorPaddedDesc.Flags |= TexCreate_RenderTargetable;
		PaddedInputColor.Texture = GraphBuilder.CreateTexture(
			ColorPaddedDesc, TEXT("ArmNssPaddedInputSceneColor"), ERDGTextureFlags::MultiFrame);
		// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
		PaddedInputColor.ViewRect = FIntRect(FIntPoint::ZeroValue, ColorPaddedDesc.Extent);
		FRDGTextureDesc VelocityPaddedDesc = SceneVelocity->Desc;
		VelocityPaddedDesc.Extent = PaddedInputSize;
		PaddedInputVelocity.Texture = GraphBuilder.CreateTexture(
			VelocityPaddedDesc, TEXT("ArmNssPaddedInputSceneVelocity"), ERDGTextureFlags::MultiFrame);
		// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
		PaddedInputVelocity.ViewRect = FIntRect(FIntPoint::ZeroValue, VelocityPaddedDesc.Extent);
		FRDGTextureDesc DepthPaddedDesc = SceneDepth->Desc;
		DepthPaddedDesc.Format = PF_DepthStencil;
		DepthPaddedDesc.Extent = PaddedInputSize;
		DepthPaddedDesc.Flags = DepthPaddedDesc.Flags & ~TexCreate_UAV;
		DepthPaddedDesc.Flags = DepthPaddedDesc.Flags & ~TexCreate_RenderTargetable;
		DepthPaddedDesc.Flags |= TexCreate_DepthStencilTargetable;
		PaddedInputDepth.Texture = GraphBuilder.CreateTexture(
			DepthPaddedDesc, TEXT("ArmNssPaddedInputSceneDepth"), ERDGTextureFlags::MultiFrame);
		// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
		PaddedInputDepth.ViewRect = FIntRect(FIntPoint::ZeroValue, DepthPaddedDesc.Extent);
		FNssMirrorPadPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNssMirrorPadPS::FParameters>();
		PassParameters->InSceneColor =
			GetScreenPassTextureInput(ScreenPassSceneColor, TStaticSamplerState<SF_Point>::GetRHI());
		PassParameters->InSceneVelocity =
			GetScreenPassTextureInput(ScreenPassSceneVelocity, TStaticSamplerState<SF_Point>::GetRHI());
		PassParameters->InSceneDepth =
			GetScreenPassTextureInput(ScreenPassSceneDepth, TStaticSamplerState<SF_Point>::GetRHI());
		PassParameters->RenderTargets[0] =
			FRenderTargetBinding(PaddedInputColor.Texture, ERenderTargetLoadAction::ENoAction);
		PassParameters->RenderTargets[1] =
			FRenderTargetBinding(PaddedInputVelocity.Texture, ERenderTargetLoadAction::ENoAction);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(PaddedInputDepth.Texture,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		TShaderMapRef<FNssMirrorPadPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("ArmNss mirror pad"),
			PixelShader,
			PassParameters,
			FIntRect(FIntPoint::ZeroValue, PaddedInputSize),
			nullptr,
			nullptr,
			TStaticDepthStencilState<true, CF_Always>::GetRHI());
	}
	NSSStateRef CurrentNSSState;
	const TRefCountPtr<INSSCustomHistory> PrevCustomHistory = View.PrevViewInfo.CustomTemporalAAHistory;
	NSSHistory* CustomHistory = static_cast<NSSHistory*>(PrevCustomHistory.GetReference());
	bool HasValidContext = CustomHistory && CustomHistory->GetState().IsValid();
	//--------------------------------------------------------------------------------------------------------------
	// Initialize the NSS Context
	//   If a context has never been created, or if significant features of the frame have changed since the current
	//   context was created, tear down any existing contexts and create a new one matching the current frame.
	//--------------------------------------------------------------------------------------------------------------
	{
		ffxApiCreateContextDescNss Params;
		FMemory::Memzero(Params);
		Params.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_NSS;
		//----------------------------------------------------------------------------------------------------------
		// Describe the Current Frame
		//   Collect the features of the current frame and the current NSS history,
		//   so we can make decisions about whether any existing NSS context is currently usable.
		//----------------------------------------------------------------------------------------------------------
		// NSS settings
		{
			Params.flags = 0;
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_QUANTIZED; // Currently we only support quantized model.
			Params.flags |= bool(ERHIZBuffer::IsInverted) ? FFX_API_NSS_CONTEXT_FLAG_DEPTH_INVERTED : 0;
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_HIGH_DYNAMIC_RANGE | FFX_API_NSS_CONTEXT_FLAG_DEPTH_INFINITE;
#if !PLATFORM_WINDOWS
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_ALLOW_16BIT;
#endif
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_READ_TENSORS_AS_IMAGES;
			// Final resolution (upscaled)
			Params.maxUpscaleSize.height = PaddedOutputSize.Y;
			Params.maxUpscaleSize.width = PaddedOutputSize.X;
			// Render resolution (downscaled)
			Params.maxRenderSize.height = PaddedInputSize.Y;
			Params.maxRenderSize.width = PaddedInputSize.X;
#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE || WITH_EDITOR
			Params.flags |= FFX_API_NSS_CONTEXT_FLAG_ENABLE_DEBUG_CHECKING;
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
			if ((CustomHistory->GetState()->LastUsedFrame == GFrameCounterRenderThread)
				|| IsNssContextParamsChanged(CurrentParams, Params))
			{
				HasValidContext = false;
			}
			else
			{
				CurrentNSSState = CustomHistory->GetState();
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
					// States that can't be trivially reused need to just be released to save memory.
					DisposeStates.Add(State);
				}
				else
				{
					CurrentNSSState = State;
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
		}
		CurrentNSSState->LastUsedFrame = GFrameCounterRenderThread;
		CurrentNSSState->ViewID = View.ViewState->UniqueID;
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
			if (!View.ViewState->PrevFrameViewInfo.CustomTemporalAAHistory.GetReference())
			{
				View.ViewState->PrevFrameViewInfo.CustomTemporalAAHistory =
					new NSSHistory(CurrentNSSState, const_cast<NSS*>(this));
			}
		}
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
				FScreenPassTexture FullResOutputs = BlankOutput(GraphBuilder, PassInputs, OutputExtents);
				*OutSceneColorTexture = FullResOutputs.Texture;
				*OutSceneColorViewRect = FullResOutputs.ViewRect;
				*OutSceneColorHalfResTexture = nullptr;
				*OutSceneColorHalfResViewRect = FIntRect::DivideAndRoundUp(*OutSceneColorViewRect, 2);
				return;
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
	ffxApiDispatchDescNss* NssDispatchParamsPtr = new ffxApiDispatchDescNss;
	ffxApiDispatchDescNss& NssDispatchParams = *NssDispatchParamsPtr;
	FMemory::Memzero(NssDispatchParams);
	{
		NssDispatchParams.header.type = FFX_API_DISPATCH_DESC_TYPE_NSS;
		NssDispatchParams.flags = 0;
		NssDispatchParams.flags |= bRenderDebugViews ? FFX_API_NSS_DISPATCH_FLAG_DRAW_DEBUG_VIEW : 0;
		// Whether to abandon the history in the state on camera cuts
		NssDispatchParams.reset = !bHistoryValid;
		NssDispatchParams.frameTimeDelta = View.Family->DeltaWorldTime * 1000.f;
		// Reference shaders use subtraction of jitter and it's in input resolution units, instead of UV units.
		NssDispatchParams.jitterOffset.x = -View.TemporalJitterPixels.X;
		NssDispatchParams.jitterOffset.y = -View.TemporalJitterPixels.Y;
		NssDispatchParams.renderSize.width = PaddedInputSize.X;
		NssDispatchParams.renderSize.height = PaddedInputSize.Y;
		NssDispatchParams.upscaleSize.width = PaddedOutputSize.X;
		NssDispatchParams.upscaleSize.height = PaddedOutputSize.Y;
		// Parameters for motion vectors:
		NssDispatchParams.motionVectorScale.x = PaddedInputSize.X;
		NssDispatchParams.motionVectorScale.y = PaddedInputSize.Y;
		NssDispatchParams.cameraFovAngleVertical = View.ViewMatrices.ComputeHalfFieldOfViewPerAxis().Y * 2.0f;
		if (bool(ERHIZBuffer::IsInverted))
		{
			NssDispatchParams.cameraNear = FLT_MAX;
			NssDispatchParams.cameraFar = GNearClippingPlane;
		}
		else
		{
			NssDispatchParams.cameraNear = GNearClippingPlane;
			NssDispatchParams.cameraFar = FLT_MAX;
		}
	}
	//------------------------------
	// Add NSS to the RenderGraph
	//------------------------------
	FRDGTextureDesc PaddedOutputColorDesc = SceneColor->Desc;
	PaddedOutputColorDesc.Extent = PaddedOutputSize;
	PaddedOutputColorDesc.Flags = TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable;
	PaddedOutputColorDesc.Format = EPixelFormat::PF_FloatR11G11B10;
	FRDGTextureRef PaddedOutputColor = GraphBuilder.CreateTexture(
		PaddedOutputColorDesc, TEXT("ArmNSSPaddedOutputSceneColor"), ERDGTextureFlags::MultiFrame);
	NSSPass::FParameters* PassParameters = GraphBuilder.AllocParameters<NSSPass::FParameters>();
	PassParameters->ColorTexture = PaddedInputColor.Texture;
	PassParameters->DepthTexture = PaddedInputDepth.Texture;
	PassParameters->VelocityTexture = PaddedInputVelocity.Texture;
	PassParameters->OutputTexture = PaddedOutputColor;
	FRDGTextureRef DebugViews{};
	if (bRenderDebugViews)
	{
		DebugViews =
			GraphBuilder.CreateTexture(PaddedOutputColorDesc, TEXT("ArmNSSDebugViews"), ERDGTextureFlags::MultiFrame);
		PassParameters->DebugViewsTexture = DebugViews;
	}
	if (CustomHistory != nullptr && CustomHistory->PaddedUpscaledColour.IsValid())
	{
		PassParameters->OutputTm1Texture = GraphBuilder.RegisterExternalTexture(CustomHistory->PaddedUpscaledColour);
	}
	else
	{
		PassParameters->OutputTm1Texture = GSystemTextures.GetBlackDummy(GraphBuilder);
	}
	if (CustomHistory != nullptr && CustomHistory->PaddedDepth.IsValid())
	{
		PassParameters->DepthTm1Texture = GraphBuilder.RegisterExternalTexture(CustomHistory->PaddedDepth);
	}
	else
	{
		PassParameters->DepthTm1Texture = GSystemTextures.GetBlackDummy(GraphBuilder);
	}
	auto* ApiAccess = ApiAccessor;
	auto CurrentApi = Api;
	if (CurrentApi == EFFXBackendAPI::Vulkan)
	{
		//------------------------------------------------------------------------------------------------------
		// Consolidate Motion Vectors
		//   UE4 motion vectors are in sparse format by default.  Convert them to a format consumable by NSS.
		//------------------------------------------------------------------------------------------------------
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(PaddedInputSize,
			PF_G16R16F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
		FRDGTextureRef MotionVectorTexture = GraphBuilder.CreateTexture(Desc, TEXT("NSSMotionVectorTexture"));
		{
			FNssConvertVelocity::FParameters* MvPassParameters =
				GraphBuilder.AllocParameters<FNssConvertVelocity::FParameters>();
			FRDGTextureUAVDesc OutputDesc(MotionVectorTexture);
			FRDGTextureSRVDesc DepthDesc = FRDGTextureSRVDesc::Create(PaddedInputDepth.Texture);
			FRDGTextureSRVDesc VelocityDesc = FRDGTextureSRVDesc::Create(PaddedInputVelocity.Texture);
			MvPassParameters->DepthTexture = PaddedInputDepth.Texture;
			MvPassParameters->InputDepth = GraphBuilder.CreateSRV(DepthDesc);
			MvPassParameters->InputVelocity = GraphBuilder.CreateSRV(VelocityDesc);
			FIntPoint Size = PaddedInputVelocity.ViewRect.Size();
			MvPassParameters->InvContentSize = FVector2D(1.0f / float(Size.X), 1.0f / float(Size.Y));
			MvPassParameters->View = View.ViewUniformBuffer;
			const FScreenPassRenderTarget MotionVectorNewRT(
				MotionVectorTexture, PaddedInputVelocity.ViewRect, ERenderTargetLoadAction::ENoAction);
			MvPassParameters->RenderTargets[0] = MotionVectorNewRT.GetRenderTargetBinding();
			TShaderMapRef<FNssConvertVelocity> ConvertVelocityShader(ShaderMap);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
				ShaderMap,
				RDG_EVENT_NAME("ArmNG ConvertVelocity (PS)"),
				ConvertVelocityShader,
				MvPassParameters,
				PaddedInputVelocity.ViewRect);
		}
		PassParameters->VelocityTexture = MotionVectorTexture;
		PassParameters->ExposureValue = View.PreExposure;
		GraphBuilder.AddPass(RDG_EVENT_NAME("ArmNG NSS (VK backend)"),
			PassParameters,
			ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
			[&View, &PassInputs, CurrentApi, ApiAccess, PassParameters, NssDispatchParamsPtr, CurrentNSSState](
				FRHICommandListImmediate& RHICmdList)
			{
				ffxApiDispatchDescNss DispatchParams = *NssDispatchParamsPtr;
				delete NssDispatchParamsPtr;
				DispatchParams.color = ApiAccess->GetNativeResource(
					PassParameters->ColorTexture->GetRHI(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
				DispatchParams.depth = ApiAccess->GetNativeResource(
					PassParameters->DepthTexture->GetRHI(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
				DispatchParams.depthTm1 = ApiAccess->GetNativeResource(
					PassParameters->DepthTm1Texture->GetRHI(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
				DispatchParams.motionVectors = ApiAccess->GetNativeResource(
					PassParameters->VelocityTexture->GetRHI(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
				DispatchParams.outputTm1 = ApiAccess->GetNativeResource(
					PassParameters->OutputTm1Texture.GetTexture(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
				DispatchParams.output = ApiAccess->GetNativeResource(
					PassParameters->OutputTexture.GetTexture(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
				DispatchParams.exposure = PassParameters->ExposureValue;
				PassParameters->ColorTexture->MarkResourceAsUsed();
				PassParameters->DepthTexture->MarkResourceAsUsed();
				PassParameters->DepthTm1Texture->MarkResourceAsUsed();
				PassParameters->VelocityTexture->MarkResourceAsUsed();
				PassParameters->OutputTm1Texture->MarkResourceAsUsed();
				PassParameters->OutputTexture->MarkResourceAsUsed();
				if (PassParameters->DebugViewsTexture)
				{
					DispatchParams.debugViews = ApiAccess->GetNativeResource(
						PassParameters->DebugViewsTexture.GetTexture(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
					PassParameters->DebugViewsTexture->MarkResourceAsUsed();
				}
				ApiAccess->ForceUAVTransition(
					RHICmdList, PassParameters->OutputTexture.GetTexture()->GetRHI(), ERHIAccess::UAVMask);
				RHICmdList.EnqueueLambda(
					[ApiAccess, CurrentNSSState, DispatchParams](FRHICommandListImmediate& cmd) mutable
					{
						DispatchParams.commandList = ApiAccess->GetNativeCommandBuffer(cmd, nullptr);
						const auto Code = ApiAccess->ffxDispatch(&CurrentNSSState->Nss, &DispatchParams.header);
						check(Code == FFX_OK);
					});
				RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
			});
	}
	else
	{
		delete NssDispatchParamsPtr;
	}
	FScreenPassTexture FullResOutputs;
	if (bRenderDebugViews)
	{
		// Output Debug Views
		FullResOutputs = CopyAndCropIfNeeded(
			GraphBuilder, FScreenPassTexture(DebugViews), PaddingOnOutput, TEXT("ArmNssOutputDebugViews"));
	}
	else
	{
		// Output Final Colour
		FullResOutputs = CopyAndCropIfNeeded(
			GraphBuilder, FScreenPassTexture(PaddedOutputColor), PaddingOnOutput, TEXT("ArmNssOutputSceneColor"));
	}
	*OutSceneColorTexture = FullResOutputs.Texture;
	*OutSceneColorViewRect = FullResOutputs.ViewRect;
	*OutSceneColorHalfResTexture = nullptr;
	*OutSceneColorHalfResViewRect = FIntRect::DivideAndRoundUp(*OutSceneColorViewRect, 2);
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
			PaddedOutputColor, &View.ViewState->PrevFrameViewInfo.TemporalAAHistory.RT[0]);
		check(IsValidRef(View.ViewState->PrevFrameViewInfo.CustomTemporalAAHistory));
		NSSHistory* NewHistory =
			static_cast<NSSHistory*>(View.ViewState->PrevFrameViewInfo.CustomTemporalAAHistory.GetReference());
		GraphBuilder.QueueTextureExtraction(PaddedOutputColor, &NewHistory->PaddedUpscaledColour);
		GraphBuilder.QueueTextureExtraction(PaddedInputDepth.Texture, &NewHistory->PaddedDepth);
	}

	View.ViewState->TemporalAASampleIndex = FMath::Clamp(View.ViewState->TemporalAASampleIndex, int8(0), MAX_int8);

	DeferredCleanup(GFrameCounterRenderThread);
	return;
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

IScreenSpaceDenoiser::FDiffuseIndirectOutputs NSS::DenoiseDiffuseIndirect(FRDGBuilder& GraphBuilder,
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

IScreenSpaceDenoiser::FDiffuseIndirectOutputs NSS::DenoiseReflectedSkyLight(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseReflectedSkyLight(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

IScreenSpaceDenoiser::FDiffuseIndirectHarmonic NSS::DenoiseDiffuseIndirectHarmonic(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectHarmonic& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseDiffuseIndirectHarmonic(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

bool NSS::SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const
{
	return WrappedDenoiser->SupportsScreenSpaceDiffuseIndirectDenoiser(Platform);
}

IScreenSpaceDenoiser::FDiffuseIndirectOutputs NSS::DenoiseScreenSpaceDiffuseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return WrappedDenoiser->DenoiseScreenSpaceDiffuseIndirect(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
