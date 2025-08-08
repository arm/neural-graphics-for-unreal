// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "NSS.h"

#include "SceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "RenderGraphUtils.h"
#include "NNE.h"
#include "NNERuntimeRDG.h"
#include "NNEModelData.h"
#include "NNETypes.h"
#include "UObject/ConstructorHelpers.h"
#include "ScreenPass.h"
#include "TemporalUpscaler.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SystemTextures.h"
#include "PixelShaderUtils.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "ISettingsModule.h"
#include "Editor.h"
#include "UObject/SavePackage.h"
#endif

#define LOCTEXT_NAMESPACE "FNSSModule"
DEFINE_LOG_CATEGORY_STATIC(LogNSS, Log, All);


// Needs to be same pointer value used for both places where this is used.
const TCHAR* NSSName = TEXT("NSS");

TAutoConsoleVariable<int32> CVarNSSDebug(
	TEXT("r.NSS.Debug"), 0,
	TEXT("Show intermediate results (0 = off, 1 = some, 2 = all, 3+ = single texture/buffer fullscreen)."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSEnable(
	TEXT("r.NSS.Enable"), 1,
	TEXT("Turn on NSS."),
	ECVF_RenderThreadSafe);

class FNSSMirrorPadPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNSSMirrorPadPS);
	SHADER_USE_PARAMETER_STRUCT(FNSSMirrorPadPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneColor)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneVelocity)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneDepth)
		SHADER_PARAMETER(FIntPoint, PaddingAfter)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNSSMirrorPadPS, "/Plugin/NSS/Private/NSSMirrorPad.usf", "MirrorPadPS", SF_Pixel);


BEGIN_SHADER_PARAMETER_STRUCT(FNSSPreprocessParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneColor)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneVelocity)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneDepth)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneDepthSampler)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InPrevFrameSceneDepth)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InPrevFrameUpscaledSceneColour)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InPrevLumaDerivativeAndLuma)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InPrevFrameClosestDepthOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InFeedback)
	SHADER_PARAMETER(FVector2f, PrevFrameJitterPixels)
	SHADER_PARAMETER(int, bCameraCut)
	SHADER_PARAMETER(FIntPoint, UnpaddedInputSize)
	SHADER_PARAMETER(FIntPoint, UnpaddedOutputSize)
	SHADER_PARAMETER(float, DisocclusionMaskDepthSeparationConstant)
	SHADER_PARAMETER(float, DisocclusionMaskPowerConstant)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutPreprocessed)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutLumaDerivativeAndLuma)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutClosestDepthOffset)

END_SHADER_PARAMETER_STRUCT()

class FNSSPreprocessCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNSSPreprocessCS);
	SHADER_USE_PARAMETER_STRUCT(FNSSPreprocessCS, FGlobalShader);

	class FQuantized : SHADER_PERMUTATION_BOOL("QUANTIZED");
	using FPermutationDomain = TShaderPermutationDomain<FQuantized>;

	using FParameters = FNSSPreprocessParameters;
};

IMPLEMENT_GLOBAL_SHADER(FNSSPreprocessCS, "/Plugin/NSS/Private/NSSPreprocess.usf", "MainCS", SF_Compute);


BEGIN_SHADER_PARAMETER_STRUCT(FNSSPostprocessParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InThetaAlpha)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InKPNFilterCol3)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InKPNFilterCol2)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InKPNFilterCol1)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InKPNFilterCol0)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneColor)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneVelocity)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneDepth)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InPrevFrameUpscaledSceneColour)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InClosestDepthOffset)
	SHADER_PARAMETER(int, bCameraCut)
	SHADER_PARAMETER(FVector2f, JitterPixels)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutSceneColor)

	SHADER_PARAMETER(FIntPoint, OutputSize)
	SHADER_PARAMETER(FIntPoint, UnpaddedInputSize)
	SHADER_PARAMETER(FIntPoint, UnpaddedOutputSize)
END_SHADER_PARAMETER_STRUCT()


class FNSSPostprocessCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNSSPostprocessCS);
	SHADER_USE_PARAMETER_STRUCT(FNSSPostprocessCS, FGlobalShader);

	class FQuantized : SHADER_PERMUTATION_BOOL("QUANTIZED");
	using FPermutationDomain = TShaderPermutationDomain<FQuantized>;

	using FParameters = FNSSPostprocessParameters;
};

IMPLEMENT_GLOBAL_SHADER(FNSSPostprocessCS, "/Plugin/NSS/Private/NSSPostprocess.usf", "MainCS", SF_Compute);


class FNSSDebugVisualizeDepthOffsetTexturePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNSSDebugVisualizeDepthOffsetTexturePS);
	SHADER_USE_PARAMETER_STRUCT(FNSSDebugVisualizeDepthOffsetTexturePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputDepthOffsetTexture)
		SHADER_PARAMETER(FIntPoint, InputDepthOffsetTextureSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FNSSDebugVisualizeDepthOffsetTexturePS, "/Plugin/NSS/Private/NSSDebugVisualize.usf", "VisualizeDepthOffsetTexturePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FNSSDebugVisualizeBufferParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InBuffer)
	SHADER_PARAMETER(FUintVector3, BufferSizeXYZ)
	SHADER_PARAMETER(uint32_t, FirstChannel)
	SHADER_PARAMETER(uint32_t, NumChannels)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FNSSDebugVisualizeBufferPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNSSDebugVisualizeBufferPS);
	SHADER_USE_PARAMETER_STRUCT(FNSSDebugVisualizeBufferPS, FGlobalShader);

	class FQuantized : SHADER_PERMUTATION_BOOL("QUANTIZED");
	using FPermutationDomain = TShaderPermutationDomain<FQuantized>;

	using FParameters = FNSSDebugVisualizeBufferParameters;
};

IMPLEMENT_GLOBAL_SHADER(FNSSDebugVisualizeBufferPS, "/Plugin/NSS/Private/NSSDebugVisualize.usf", "VisualizeBufferPS", SF_Pixel);

FIntPoint RoundUpToMultiple(FIntPoint in, uint32 multiple)
{
	return ((in + multiple - 1) / multiple) * multiple;
}

struct NSSModel
{
	TSharedPtr<UE::NNE::IModelInstanceRDG> ModelInstance;

	struct OutputIndices {
		int32_t Feedback = -1;
		int32_t ThetaAlpha = -1;
		int32_t KPNFilterCol3 = -1;
		int32_t KPNFilterCol2 = -1;
		int32_t KPNFilterCol1 = -1;
		int32_t KPNFilterCol0 = -1;
	};

	// The NSS model has multiple outputs and unfortunately the order of these outputs depend on how the 
	// model was created. These variables tell us which is which.
	OutputIndices OutputIndices;
};

TSharedPtr<NSSModel> CreateNSSModelFromAsset(UNNEModelData* ModelData)
{
	check(ModelData);

	const FString& NNERuntimeName = GetDefault<UNSSSettings>()->NNERuntime;

	if (!UE::NNE::GetAllRuntimeNames().Contains(NNERuntimeName))
	{
		// Note we must check this manually, as UE::NNE::GetRuntime() will assert if an invalid name is passed.
		UE_LOG(LogNSS, Error, TEXT("Couldn't find the requested NNE runtime: %s"), *NNERuntimeName);
		return {};
	}

	TWeakInterfacePtr<INNERuntimeRDG> runtime = UE::NNE::GetRuntime<INNERuntimeRDG>(NNERuntimeName);
	if (!runtime.IsValid())
	{
		UE_LOG(LogNSS, Error, TEXT("Error retrieving the requested NNE runtime: %s"), *NNERuntimeName);
		return {};
	}

	TSharedPtr<UE::NNE::IModelRDG> Model = runtime->CreateModelRDG(ModelData);
	if (!Model.IsValid())
	{
		UE_LOG(LogNSS, Error, TEXT("Failed to create the NSS model using runtime: %s"), *NNERuntimeName);
		return {};
	}

	TSharedPtr<UE::NNE::IModelInstanceRDG> ModelInstance = Model->CreateModelInstanceRDG();
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogNSS, Error, TEXT("Failed to create the NSS model instance using runtime: %s"), *NNERuntimeName);
		return {};
	}

	TSharedPtr<NSSModel> Result = MakeShared<NSSModel>();
	Result->ModelInstance = ModelInstance;

	if (Result->ModelInstance->GetInputTensorDescs().Num() > 0)
	{
		// Hardcode which output is which based on the model that we provide.
		bool IsQuantized = Result->ModelInstance->GetInputTensorDescs()[0].GetElementByteSize() == 1;
		if (IsQuantized)
		{
			Result->OutputIndices.Feedback = 0;
			Result->OutputIndices.ThetaAlpha = 1;
			Result->OutputIndices.KPNFilterCol3 = 2;
			Result->OutputIndices.KPNFilterCol2 = 3;
			Result->OutputIndices.KPNFilterCol1 = 4;
			Result->OutputIndices.KPNFilterCol0 = 5;
		}
		else
		{
			Result->OutputIndices.Feedback = 5;
			Result->OutputIndices.ThetaAlpha = 4;
			Result->OutputIndices.KPNFilterCol3 = 3;
			Result->OutputIndices.KPNFilterCol2 = 2;
			Result->OutputIndices.KPNFilterCol1 = 1;
			Result->OutputIndices.KPNFilterCol0 = 0;
		}
	}


	return Result;
}

/// History written by frame N and read by frame N+1.
class FNSSTemporalAAHistory : public UE::Renderer::Private::ITemporalUpscaler::IHistory, public FRefCountBase
{
public:
	const TCHAR* GetDebugName() const override
	{
		return NSSName;
	}
	uint64 GetGPUSizeBytes() const override
	{
		return 0;
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

	TRefCountPtr<IPooledRenderTarget> PaddedUpscaledColour; // No view rect associated here - always the full thing.
	TRefCountPtr<IPooledRenderTarget> PaddedDepth; // View rect is specified by PaddedDepthViewRect .
	FIntRect PaddedDepthViewRect; // Might be smaller than the texture extent (e.g. tiling quantisation).
	TRefCountPtr<IPooledRenderTarget> LumaDerivativeAndLuma; // No view rect associated here - always the full thing.
	TRefCountPtr<IPooledRenderTarget> ClosestDepthOffset; // No view rect associated here - always the full thing.
	TRefCountPtr<FRDGPooledBuffer> Feedback;
	FVector2f JitterPixels;
};

class FNSSTemporalUpscaler : public UE::Renderer::Private::ITemporalUpscaler
{
public:
	FNSSTemporalUpscaler(TSharedPtr<NSSModel> NSSModel, bool UseHistoryIfAvailable)
		: NSSModel(NSSModel)
		, UseHistoryIfAvailable(UseHistoryIfAvailable)
	{
	}

	virtual const TCHAR* GetDebugName() const
	{
		return NSSName;
	}

	TSharedPtr<NSSModel> GetNSSModel() const
	{
		return NSSModel;
	}

	FOutputs Failure(FRDGBuilder& GraphBuilder,
		const FInputs& Inputs) const
	{
		FOutputs Outputs;

		FRDGTextureDesc OutputColorDesc = Inputs.SceneColor.Texture->Desc;
		OutputColorDesc.Extent = Inputs.OutputViewRect.Size();
		OutputColorDesc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Outputs.FullRes.Texture = GraphBuilder.CreateTexture(
			OutputColorDesc,
			TEXT("NSSDisabledOutputSceneColor"),
			ERDGTextureFlags::MultiFrame);
		Outputs.FullRes.ViewRect = Inputs.OutputViewRect;

		AddClearRenderTargetPass(GraphBuilder, Outputs.FullRes.Texture, FLinearColor(1.0f, 1.0f, 0.0f));

		AreDebugTexturesValid = false;
		DebugPreprocessedBuffer = nullptr;
		for (int I = 0; I < DebugNetworkOutputBuffers.Num(); ++I)
		{
			DebugNetworkOutputBuffers[I] = nullptr;
		}
		DebugClosestDepthOffset = nullptr;

		Outputs.NewHistory = new FNSSTemporalAAHistory();

		return Outputs;
	}

	virtual FOutputs AddPasses(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FInputs& Inputs) const
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		FOutputs Outputs;

		// Note that the texture extent might be LARGER than the ViewRect, as in the editor it won't shrink the render target if the 
		// viewport is shrunk (as an optimisation presumably).

		// The network requires the inputs to be a multiple of 8 in both width and height (i.e. a 540p input frame should be padded to 544) with mirroring
		// if padding is required. The padding is always done on the bottom/right.
		// The post-processing also produces a padded output, with the padding amount upscaled from the input padding amount.
		// Unfortunately the calculation for the output size is more complex. 
		// Since the input of the network can be padded by up to 7 pixels the output will be cropped
		// up to 7 times the ratio of input resolution to output resolution
		// e.g. input res 545 upscaled with a ratio of 2 to 1090
		//      545 pads to 552. 
		//      552 * 2 =  1104
		//      1104 - 1090 = 14
		FIntPoint PaddedInputSize = RoundUpToMultiple(Inputs.SceneColor.ViewRect.Size(), 8);
		FIntPoint PaddingOnInput = PaddedInputSize - Inputs.SceneColor.ViewRect.Size();

		FIntPoint PaddingOnOutput;
		PaddingOnOutput.X = FMath::DivideAndRoundUp(PaddingOnInput.X * Inputs.OutputViewRect.Width(), Inputs.SceneColor.ViewRect.Width());
		PaddingOnOutput.Y = FMath::DivideAndRoundUp(PaddingOnInput.Y * Inputs.OutputViewRect.Height(), Inputs.SceneColor.ViewRect.Height());
		FIntPoint PaddedOutputSize = Inputs.OutputViewRect.Size() + PaddingOnOutput;

		// Set the input shapes on the model, if none set or the resolution has changed since last frame.
		if (NSSModel->ModelInstance->GetInputTensorShapes().IsEmpty() ||
			NSSModel->ModelInstance->GetInputTensorShapes()[0].GetData()[1] != PaddedInputSize.Y
			|| NSSModel->ModelInstance->GetInputTensorShapes()[0].GetData()[2] != PaddedInputSize.X)
		{
			TArray<UE::NNE::FTensorShape> inputShapes = { UE::NNE::FTensorShape::Make(
				TArray<uint32_t>{1, uint32_t(PaddedInputSize.Y), uint32_t(PaddedInputSize.X), 12}) };
			if (NSSModel->ModelInstance->SetInputTensorShapes(inputShapes) != UE::NNE::IModelInstanceRDG::ESetInputTensorShapesStatus::Ok)
			{
				UE_LOG(LogNSS, Error, TEXT("Failed to set the input tensor shapes for the NSS model instance"));
				return Failure(GraphBuilder, Inputs);
			}

			// Print the inferred shapes.
			for (int I = 0; I < NSSModel->ModelInstance->GetInputTensorDescs().Num(); ++I)
			{
				FString shape;
				for (auto d : NSSModel->ModelInstance->GetInputTensorShapes()[I].GetData())
				{
					shape += FString::FromInt(d) + " x ";
				}
				UE_LOG(LogNSS, Log, TEXT("Input %s: %s"), *NSSModel->ModelInstance->GetInputTensorDescs()[I].GetName(), *shape);
			}
			for (int I = 0; I < NSSModel->ModelInstance->GetOutputTensorDescs().Num(); ++I)
			{
				FString shape;
				for (auto d : NSSModel->ModelInstance->GetOutputTensorShapes()[I].GetData())
				{
					shape += FString::FromInt(d) + " x ";
				}
				UE_LOG(LogNSS, Log, TEXT("Output %s: %s"), *NSSModel->ModelInstance->GetOutputTensorDescs()[I].GetName(), *shape);
			}
		}

		// Validate that the width/height of the outputs are as expected (should be the same as the input).
		auto&& OutputShapes = NSSModel->ModelInstance->GetOutputTensorShapes();
		for (int OutputIdx = 0; OutputIdx < OutputShapes.Num(); ++OutputIdx)
		{
			if (OutputShapes[OutputIdx].GetData() != TArray<uint32>{1, uint32(PaddedInputSize.Y), uint32(PaddedInputSize.X), 4})
			{
				UE_LOG(LogNSS, Error, TEXT("Output tensor shapes for the NSS model instance are wrong!"));
				return Failure(GraphBuilder, Inputs);
			}
		}

		// Copy the input scene color, depth and velocity textures and add padding around the edges if necessary.
		FScreenPassTexture PaddedInputColor = Inputs.SceneColor;
		FScreenPassTexture PaddedInputDepth = Inputs.SceneDepth;
		FScreenPassTexture PaddedInputVelocity = Inputs.SceneVelocity;
		if (PaddingOnInput != FIntPoint::ZeroValue)
		{
			// Note we base the new extents on the ViewRect size, not the extent so we don't unnecesarrily make a bigger texture 
			// if not all of the input texture was actually being used.
			// This means the padded output texture could actually be smaller than the input texture extents!

			FRDGTextureDesc ColorPaddedDesc = Inputs.SceneColor.Texture->Desc;
			ColorPaddedDesc.Extent = Inputs.SceneColor.ViewRect.Size() + PaddingOnInput;
			ColorPaddedDesc.Flags |= TexCreate_RenderTargetable;
			PaddedInputColor.Texture = GraphBuilder.CreateTexture(ColorPaddedDesc, TEXT("NSSPaddedInputSceneColor"), ERDGTextureFlags::MultiFrame);
			// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
			PaddedInputColor.ViewRect = FIntRect(FIntPoint::ZeroValue, ColorPaddedDesc.Extent);

			FRDGTextureDesc VelocityPaddedDesc = Inputs.SceneVelocity.Texture->Desc;
			VelocityPaddedDesc.Extent = Inputs.SceneVelocity.ViewRect.Size() + PaddingOnInput;
			VelocityPaddedDesc.Flags |= TexCreate_RenderTargetable;
			PaddedInputVelocity.Texture = GraphBuilder.CreateTexture(VelocityPaddedDesc, TEXT("NSSPaddedInputSceneVelocity"), ERDGTextureFlags::MultiFrame);
			// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
			PaddedInputVelocity.ViewRect = FIntRect(FIntPoint::ZeroValue, VelocityPaddedDesc.Extent);

			FRDGTextureDesc DepthPaddedDesc = Inputs.SceneDepth.Texture->Desc;
			// We copy most settings from the input depth texture, but in some cases (e.g. replaying frames for testing)
			// these may be not be compatible with binding as a depth target, so we overwrite/fix them.
			DepthPaddedDesc.Format = PF_DepthStencil;
			DepthPaddedDesc.Extent = Inputs.SceneDepth.ViewRect.Size() + PaddingOnInput;
			DepthPaddedDesc.Flags = DepthPaddedDesc.Flags & ~TexCreate_UAV;
			DepthPaddedDesc.Flags = DepthPaddedDesc.Flags & ~TexCreate_RenderTargetable;
			DepthPaddedDesc.Flags |= TexCreate_DepthStencilTargetable;
			PaddedInputDepth.Texture = GraphBuilder.CreateTexture(DepthPaddedDesc, TEXT("NSSPaddedInputSceneDepth"), ERDGTextureFlags::MultiFrame);
			// Note: the ViewRect on the output is the full texture, as we allocate one of the exact correct size
			PaddedInputDepth.ViewRect = FIntRect(FIntPoint::ZeroValue, DepthPaddedDesc.Extent);

			FNSSMirrorPadPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNSSMirrorPadPS::FParameters>();
			PassParameters->InSceneColor = GetScreenPassTextureInput(Inputs.SceneColor, TStaticSamplerState<SF_Point>::GetRHI());
			PassParameters->InSceneVelocity = GetScreenPassTextureInput(Inputs.SceneVelocity, TStaticSamplerState<SF_Point>::GetRHI());
			PassParameters->InSceneDepth = GetScreenPassTextureInput(Inputs.SceneDepth, TStaticSamplerState<SF_Point>::GetRHI());

			PassParameters->PaddingAfter = PaddingOnInput;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(PaddedInputColor.Texture, ERenderTargetLoadAction::ENoAction);
			PassParameters->RenderTargets[1] = FRenderTargetBinding(PaddedInputVelocity.Texture, ERenderTargetLoadAction::ENoAction);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(PaddedInputDepth.Texture, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

			TShaderMapRef<FNSSMirrorPadPS> PixelShader(ShaderMap);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("NSS mirror pad"), PixelShader,
				PassParameters, FIntRect(FIntPoint::ZeroValue, PaddedInputSize), nullptr, nullptr, TStaticDepthStencilState<true, CF_Always>::GetRHI());
		}

		bool IsQuantized = NSSModel->ModelInstance->GetInputTensorDescs()[0].GetElementByteSize() == 1;
		uint32 NumElements = NSSModel->ModelInstance->GetInputTensorShapes()[0].Volume();
		FRDGBufferRef PreprocessedBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(NSSModel->ModelInstance->GetInputTensorDescs()[0].GetElementByteSize(),
			NumElements), TEXT("NSSPreprocessedBuffer"));

		FRDGTextureDesc LumaTextureDesc = FRDGTextureDesc::Create2D(PaddedInputSize, EPixelFormat::PF_R8G8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef LumaDerivativeAndLuma = GraphBuilder.CreateTexture(LumaTextureDesc, TEXT("NSSLumaDerivativeAndLuma"));

		FRDGTextureDesc ClosestDepthOffsetDesc = FRDGTextureDesc::Create2D(PaddedInputSize, EPixelFormat::PF_R8_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef ClosestDepthOffset = GraphBuilder.CreateTexture(ClosestDepthOffsetDesc, TEXT("NSSClosestDepthOffset"));

		int32 BytesPerPixel = IsQuantized ? 4 : 16;
		// Note all the output buffers are the same size
		int32 NetworkOutputBufferSize = NSSModel->ModelInstance->GetOutputTensorShapes()[0].GetData()[1] * NSSModel->ModelInstance->GetOutputTensorShapes()[0].GetData()[2];
		FRDGBufferRef Feedback = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSFeedbackBuffer"));
		FRDGBufferRef ThetaAlpha = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSThetaAlpha"));
		FRDGBufferRef KPNFilterCol3 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSKPNFilterCol3"));
		FRDGBufferRef KPNFilterCol2 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSKPNFilterCol2"));
		FRDGBufferRef KPNFilterCol1 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSKPNFilterCol1"));
		FRDGBufferRef KPNFilterCol0 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(BytesPerPixel, NetworkOutputBufferSize), TEXT("NSSKPNFilterCol0"));


		// Get history from last frame (if present and usable).
		// If the history has changed size (e.g. screen was resized) or the camera was suddenly moved ('camera cut')
		// then disregard the history.
		FNSSTemporalAAHistory* History = nullptr;
		if (Inputs.PrevHistory.IsValid() && UseHistoryIfAvailable && !View.bCameraCut)
		{
			History = static_cast<FNSSTemporalAAHistory*>(Inputs.PrevHistory.GetReference());
			bool HistoryValid =
				History->PaddedUpscaledColour.IsValid() && History->PaddedUpscaledColour->GetDesc().Extent == PaddedOutputSize &&
				History->PaddedDepth.IsValid() && History->PaddedDepthViewRect == PaddedInputDepth.ViewRect &&
				History->LumaDerivativeAndLuma.IsValid() && History->LumaDerivativeAndLuma->GetDesc().Extent == LumaDerivativeAndLuma->Desc.Extent &&
				History->ClosestDepthOffset.IsValid() && History->ClosestDepthOffset->GetDesc().Extent == ClosestDepthOffset->Desc.Extent &&
				History->Feedback.IsValid() && History->Feedback->GetSize() == Feedback->GetSize();
			if (!HistoryValid)
			{
				History = nullptr;
			}
		}

		FNSSPreprocessParameters* PreprocessParameters = GraphBuilder.AllocParameters<FNSSPreprocessParameters>();
		PreprocessParameters->View = View.ViewUniformBuffer;

		PreprocessParameters->InSceneColor = GetScreenPassTextureInput(PaddedInputColor, TStaticSamplerState<SF_Bilinear>::GetRHI());
		PreprocessParameters->InSceneVelocity = GetScreenPassTextureInput(PaddedInputVelocity, TStaticSamplerState<SF_Point>::GetRHI());
		PreprocessParameters->InSceneDepth = GetScreenPassTextureInput(PaddedInputDepth, TStaticSamplerState<SF_Point>::GetRHI());
		if (History != nullptr)
		{
			PreprocessParameters->InPrevFrameSceneDepth = GetScreenPassTextureInput(FScreenPassTexture(
				GraphBuilder.RegisterExternalTexture(History->PaddedDepth, TEXT("NSSPrevFramePaddedDepth")), History->PaddedDepthViewRect), TStaticSamplerState<SF_Point>::GetRHI());
		}
		else
		{
			// If no history, bind the current depth as the previous depth
			PreprocessParameters->InPrevFrameSceneDepth = GetScreenPassTextureInput(PaddedInputDepth, TStaticSamplerState<SF_Point>::GetRHI());
		}

		if (History != nullptr)
		{
			PreprocessParameters->InPrevFrameUpscaledSceneColour = GetScreenPassTextureInput(FScreenPassTexture(
				GraphBuilder.RegisterExternalTexture(History->PaddedUpscaledColour, TEXT("NSSPrevFrameUpsampledColour"))),
				TStaticSamplerState<SF_Point>::GetRHI());
		}
		else
		{
			PreprocessParameters->InPrevFrameUpscaledSceneColour = GetScreenPassTextureInput(
				FScreenPassTexture(GSystemTextures.GetBlackDummy(GraphBuilder)), TStaticSamplerState<SF_Point>::GetRHI());
		}
		if (History != nullptr)
		{
			PreprocessParameters->InFeedback =
				GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(History->Feedback, TEXT("NSSPrevFrameFeedback")), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		}
		else
		{
			PreprocessParameters->InFeedback =
				(IsQuantized ? GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer<uint32_t>(GraphBuilder), EPixelFormat::PF_R32_UINT) : GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer<float>(GraphBuilder), EPixelFormat::PF_A32B32G32R32F)); //This isn't the full size, just a placeholder - we check in the shader
		}
		PreprocessParameters->PrevFrameJitterPixels = History != nullptr ? FVector2f(History->JitterPixels.X, History->JitterPixels.Y) : FVector2f(0.0f, 0.0f);
		PreprocessParameters->bCameraCut = (History == nullptr); // Note this accounts for more than just View.bCameraCut

		PreprocessParameters->UnpaddedInputSize = Inputs.SceneColor.ViewRect.Size();
		PreprocessParameters->UnpaddedOutputSize = Inputs.OutputViewRect.Size();

		// Kfov is 1 / cos(f), where f is the half-angle of the field-of-view in the diagonal direction
		FVector4f TanAndInvTanHalfFOV = View.ViewMatrices.GetTanAndInvTanHalfFOV();
		float TanDiagonalHalfFov = FVector2f(TanAndInvTanHalfFOV.X, TanAndInvTanHalfFOV.Y).Length();
		float DiagonalHalfFov = FMath::Atan(TanDiagonalHalfFov);
		float Kfov = 1.0 / FMath::Cos(DiagonalHalfFov);
		PreprocessParameters->DisocclusionMaskDepthSeparationConstant = 1.37e-05 * Kfov * FVector2f(PaddedInputSize).Size();

		float ResolutionFactor = FMath::Clamp(FVector2f(PaddedInputSize).Size() / FVector2f(1920, 1080).Size(), 0.0f, 1.0f);
		PreprocessParameters->DisocclusionMaskPowerConstant = FMath::Lerp(1.0f, 3.0f, ResolutionFactor);

		PreprocessParameters->OutLumaDerivativeAndLuma = GraphBuilder.CreateUAV(LumaDerivativeAndLuma);
		if (History != nullptr)
		{
			PreprocessParameters->InPrevLumaDerivativeAndLuma = GetScreenPassTextureInput(FScreenPassTexture(
				GraphBuilder.RegisterExternalTexture(History->LumaDerivativeAndLuma, TEXT("NSSPrevLumaDerivativeAndLuma"))), TStaticSamplerState<SF_Bilinear>::GetRHI());
		}
		else
		{
			PreprocessParameters->InPrevLumaDerivativeAndLuma = GetScreenPassTextureInput(FScreenPassTexture(
				GSystemTextures.GetBlackDummy(GraphBuilder), FIntRect(0, 0, 1, 1)), TStaticSamplerState<SF_Bilinear>::GetRHI());
		}

		PreprocessParameters->OutClosestDepthOffset = GraphBuilder.CreateUAV(ClosestDepthOffset);
		if (History != nullptr)
		{
			PreprocessParameters->InPrevFrameClosestDepthOffset = GetScreenPassTextureInput(FScreenPassTexture(
				GraphBuilder.RegisterExternalTexture(History->ClosestDepthOffset, TEXT("NSSPrevClosestDepthOffset"))), TStaticSamplerState<SF_Point>::GetRHI());
		}
		else
		{
			PreprocessParameters->InPrevFrameClosestDepthOffset = GetScreenPassTextureInput(FScreenPassTexture(
				GSystemTextures.GetBlackDummy(GraphBuilder), FIntRect(0, 0, 1, 1)), TStaticSamplerState<SF_Point>::GetRHI());
		}

		PreprocessParameters->OutPreprocessed = GraphBuilder.CreateUAV(PreprocessedBuffer, IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT);


		FNSSPreprocessCS::FPermutationDomain PreprocessPermutationVector;
		PreprocessPermutationVector.Set<FNSSPreprocessCS::FQuantized>(IsQuantized);

		TShaderMapRef<FNSSPreprocessCS> PreprocessShader(ShaderMap, PreprocessPermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("NSS Preprocess"),
			PreprocessShader,
			PreprocessParameters,
			FComputeShaderUtils::GetGroupCount(PaddedInputSize, 8));

		TStaticArray<FRDGBufferRef, 6> NetworkOutputBuffers;
		NetworkOutputBuffers[NSSModel->OutputIndices.Feedback] = Feedback;
		NetworkOutputBuffers[NSSModel->OutputIndices.ThetaAlpha] = ThetaAlpha;
		NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol3] = KPNFilterCol3;
		NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol2] = KPNFilterCol2;
		NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol1] = KPNFilterCol1;
		NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol0] = KPNFilterCol0;


		TArray<UE::NNE::FTensorBindingRDG> InputBindings;
		InputBindings.Reserve(NSSModel->ModelInstance->GetInputTensorDescs().Num());
		TArray<UE::NNE::FTensorBindingRDG> OutputBindings;
		OutputBindings.Reserve(NSSModel->ModelInstance->GetOutputTensorDescs().Num());

		UE::NNE::FTensorBindingRDG InputBinding;
		InputBinding.Buffer = PreprocessedBuffer;
		InputBindings.Push(InputBinding);

		for (int I = 0; I < NetworkOutputBuffers.Num(); ++I)
		{
			UE::NNE::FTensorBindingRDG OutputBinding;
			OutputBinding.Buffer = NetworkOutputBuffers[I];
			OutputBindings.Push(OutputBinding);
		}

		if (NSSModel->ModelInstance->EnqueueRDG(GraphBuilder, InputBindings, OutputBindings) != UE::NNE::IModelInstanceRDG::EEnqueueRDGStatus::Ok)
		{
			UE_LOG(LogNSS, Error, TEXT("Failed to run inference with NSS model instance"));
			return Failure(GraphBuilder, Inputs);
		}

		// Create output texture which includes the padding. This will be fed back into the next frame and we will also
		// take a crop of it to return to Unreal.
		FRDGTextureDesc PaddedOutputColorDesc = Inputs.SceneColor.Texture->Desc;
		PaddedOutputColorDesc.Extent = PaddedOutputSize;
		PaddedOutputColorDesc.Flags = TexCreate_ShaderResource | TexCreate_UAV;
		PaddedOutputColorDesc.Format = Inputs.SceneColor.Texture->Desc.Format;
		FRDGTextureRef PaddedOutputColor = GraphBuilder.CreateTexture(PaddedOutputColorDesc, TEXT("NSSPaddedOutputSceneColor"), ERDGTextureFlags::MultiFrame);

		FNSSPostprocessParameters* PostprocessParameters = GraphBuilder.AllocParameters<FNSSPostprocessParameters>();
		PostprocessParameters->View = View.ViewUniformBuffer;
		PostprocessParameters->InThetaAlpha = GraphBuilder.CreateSRV(NetworkOutputBuffers[NSSModel->OutputIndices.ThetaAlpha], IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		PostprocessParameters->InKPNFilterCol3 = GraphBuilder.CreateSRV(NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol3], IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		PostprocessParameters->InKPNFilterCol2 = GraphBuilder.CreateSRV(NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol2], IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		PostprocessParameters->InKPNFilterCol1 = GraphBuilder.CreateSRV(NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol1], IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		PostprocessParameters->InKPNFilterCol0 = GraphBuilder.CreateSRV(NetworkOutputBuffers[NSSModel->OutputIndices.KPNFilterCol0], IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_A32B32G32R32F);
		PostprocessParameters->InSceneColor = GetScreenPassTextureInput(PaddedInputColor, TStaticSamplerState<SF_Bilinear>::GetRHI());
		PostprocessParameters->InSceneVelocity = GetScreenPassTextureInput(PaddedInputVelocity, TStaticSamplerState<SF_Point>::GetRHI());
		PostprocessParameters->InSceneDepth = GetScreenPassTextureInput(PaddedInputDepth, TStaticSamplerState<SF_Point>::GetRHI());
		if (History != nullptr)
		{
			PostprocessParameters->InPrevFrameUpscaledSceneColour = GetScreenPassTextureInput(
				FScreenPassTexture(GraphBuilder.RegisterExternalTexture(History->PaddedUpscaledColour, TEXT("NSSPrevFramePaddedUpsampledColour"))), TStaticSamplerState<SF_Bilinear>::GetRHI());
		}
		else
		{
			PostprocessParameters->InPrevFrameUpscaledSceneColour = GetScreenPassTextureInput(
				FScreenPassTexture(GSystemTextures.GetBlackDummy(GraphBuilder)), TStaticSamplerState<SF_Bilinear>::GetRHI());
		}
		PostprocessParameters->InClosestDepthOffset = GetScreenPassTextureInput(FScreenPassTexture(ClosestDepthOffset), TStaticSamplerState<SF_Point>::GetRHI());
		PostprocessParameters->OutSceneColor = GraphBuilder.CreateUAV(PaddedOutputColor);
		PostprocessParameters->bCameraCut = (History == nullptr); // Note this accounts for more than just View.bCameraCut
		PostprocessParameters->JitterPixels = Inputs.TemporalJitterPixels;
		PostprocessParameters->OutputSize = PaddedOutputSize;
		PostprocessParameters->UnpaddedInputSize = Inputs.SceneColor.ViewRect.Size();
		PostprocessParameters->UnpaddedOutputSize = Inputs.OutputViewRect.Size();

		FNSSPostprocessCS::FPermutationDomain PostprocessPermutationVector;
		PostprocessPermutationVector.Set<FNSSPostprocessCS::FQuantized>(IsQuantized);

		TShaderMapRef<FNSSPostprocessCS> PostprocessShader(ShaderMap, PostprocessPermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("NSS Postprocess"),
			PostprocessShader,
			PostprocessParameters,
			FComputeShaderUtils::GetGroupCount(PaddedOutputSize, 8));

		// Create the final output texture, which is a cropped version of the output of the post-processing shader.
		Outputs.FullRes = FScreenPassTexture(PaddedOutputColor, Inputs.OutputViewRect);


		// Save history for next frame.
		TRefCountPtr<FNSSTemporalAAHistory> NewHistory = new FNSSTemporalAAHistory();
		GraphBuilder.QueueTextureExtraction(PaddedOutputColor, &NewHistory->PaddedUpscaledColour);
		GraphBuilder.QueueTextureExtraction(LumaDerivativeAndLuma, &NewHistory->LumaDerivativeAndLuma);
		GraphBuilder.QueueTextureExtraction(ClosestDepthOffset, &NewHistory->ClosestDepthOffset);
		GraphBuilder.QueueTextureExtraction(PaddedInputDepth.Texture, &NewHistory->PaddedDepth);
		NewHistory->PaddedDepthViewRect = PaddedInputDepth.ViewRect;
		GraphBuilder.QueueBufferExtraction(NetworkOutputBuffers[NSSModel->OutputIndices.Feedback], &NewHistory->Feedback);
		NewHistory->JitterPixels = Inputs.TemporalJitterPixels;

		Outputs.NewHistory = NewHistory;

		DebugPreprocessedBuffer = PreprocessedBuffer;
		DebugNetworkOutputBuffers = NetworkOutputBuffers;
		DebugClosestDepthOffset = ClosestDepthOffset;

		AreDebugTexturesValid = true;
		return Outputs;
	}

	virtual float GetMinUpsampleResolutionFraction() const { return 0.5f; }
	virtual float GetMaxUpsampleResolutionFraction() const { return 1.0f; }

	virtual ITemporalUpscaler* Fork_GameThread(const class FSceneViewFamily& ViewFamily) const { return new FNSSTemporalUpscaler(NSSModel, UseHistoryIfAvailable); }

	mutable FRDGBufferRef DebugPreprocessedBuffer = nullptr;
	mutable TStaticArray<FRDGBufferRef, 6> DebugNetworkOutputBuffers = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	mutable FRDGTextureRef DebugClosestDepthOffset = nullptr;

public:
	TSharedPtr<NSSModel> NSSModel;
	bool UseHistoryIfAvailable;
	mutable bool AreDebugTexturesValid = false;
};

class FNSSSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FNSSSceneViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
		// On first load, check if the model data asset exists.
		// If it doesn't, there may still be a .vgf file in the Content folder where the uasset would be, in which case
		// we automatically import it for the user. This will always be the case when first loading the plugin as we don't
		// include the uasset file when distributing.
#if WITH_EDITOR
		UNNEModelData* Asset = GetDefault<UNSSSettings>()->NSSModelData.LoadSynchronous();
		if (Asset == nullptr)
		{
			// It doesn't exist, so check if an appropriate VGF file exists instead
			const FString ContentDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NSS"))->GetBaseDir(), TEXT("Content"));
			const FString CandidateVgfPath = FPaths::Combine(ContentDir, GetDefault<UNSSSettings>()->NSSModelData.GetAssetName() + ".vgf");

			UE_LOG(LogNSS, Verbose, TEXT("Missing NSS model asset at requested path: %s. Checking for VGF file to import at %s"),
				*GetDefault<UNSSSettings>()->NSSModelData.ToString(), *CandidateVgfPath);
			if (FPaths::FileExists(CandidateVgfPath))
			{
				UE_LOG(LogNSS, Verbose, TEXT("VGF file found, importing..."));
				IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
				bool bSyncToBrowser = false; // Avoids crash if this code runs whilst cooking
				TArray<UObject*> ImportedAssets = AssetTools.ImportAssets({ CandidateVgfPath }, "/NSS", nullptr, bSyncToBrowser);
				if (ImportedAssets.IsEmpty() || ImportedAssets[0] == nullptr || ImportedAssets[0]->GetClass() != UNNEModelData::StaticClass())
				{
					UE_LOG(LogNSS, Error, TEXT("Failed to import VGF file %s"), *CandidateVgfPath);
					// The below call to RecreateModelFromAsset() will fail, and the user will have to fix the asset themselves
				}
				else
				{
					UE_LOG(LogNSS, Log, TEXT("Successfully imported VGF file %s"), *CandidateVgfPath);
					// The asset should now be available at the requested path, so the call to RecreateModelFromAsset() below should succeed.
					// Save the asset to disk, otherwise the user will be prompted to do this when they close the editor
					FString PackageFileName = FPackageName::LongPackageNameToFilename(ImportedAssets[0]->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
					FSavePackageArgs SaveArgs;
					UPackage::Save(ImportedAssets[0]->GetOutermost(), ImportedAssets[0], *PackageFileName, SaveArgs);
				}
			}
			else
			{
				UE_LOG(LogNSS, Verbose, TEXT("VGF file not found"));
				// The below call to RecreateModelFromAsset() will fail, and the user will have to fix the asset themselves
			}
		}
#endif

		// If cooking, we won't have an RHI and can't use this plugin. This would be caught at some point later, 
		// but would log an error which will fail the cooking commandlet. Instead, we detect cooking explicitly and 
		// log this at a lower severity.
		// Note this check needs to come after the shader source mapping so that the shaders can be cooked,
		// and after the auto-import code above so that auto-importing can work when cooking immediately after installing
		// the plugin.
		if (IsRunningCookCommandlet()) 
		{
			UE_LOG(LogNSS, Log, TEXT("Cooking detected - the NSS plugin will not be available."));
			return;
		}

		RecreateModelFromAsset();
	}

	void RecreateModelFromAsset()
	{
		UNNEModelData* Asset = GetDefault<UNSSSettings>()->NSSModelData.LoadSynchronous();
		if (Asset)
		{
			NSSModel = CreateNSSModelFromAsset(Asset);
			SkipHistoryNextFrame = true; // This prevents issues with NaNs creeping in from the previous model's feedback.
		}
		else
		{
			UE_LOG(LogNSS, Error, TEXT("Couldn't load the NSS model asset from %s"), *GetDefault<UNSSSettings>()->NSSModelData.ToString());
			NSSModel = nullptr;
		}
	}

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
	{
		if (!NSSModel || !NSSModel->ModelInstance)
		{
			// Model failed to load/compile (e.g. missing asset file).
			return false;
		}

		return true;
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily)
	{
	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
	{
	}


	virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily) override
	{
		bool Enable = false;
		for (int i = 0; i < ViewFamily.Views.Num(); i++)
		{
			const FSceneView* View = ViewFamily.Views[i];
			if (View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
			{
				Enable = true;
			}
		}
		if (CVarNSSEnable.GetValueOnGameThread() == 0)
		{
			Enable = false;
		}

		if (ViewFamily.GetTemporalUpscalerInterface() != nullptr)
		{
			// Another plugin has already set a temporal upscaler interface - if we try to set it again
			// then it will assert, so we have to yield.
			Enable = false;
		}

		if (Enable)
		{
			ViewFamily.SetTemporalUpscalerInterface(new FNSSTemporalUpscaler(NSSModel, !SkipHistoryNextFrame));
			SkipHistoryNextFrame = false;
		}
	}

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override
	{
		const bool bDebug = CVarNSSDebug.GetValueOnRenderThread() != 0;
		if (bDebug && Pass == EPostProcessingPass::Tonemap)
		{
			InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateStatic(&FNSSSceneViewExtension::AddDebugPostProcessPass_RenderThread));
		}
	}

private:
	// Note: this is static to avoid cross-thread access to member variables (class is owned by the game thread, but this function is called on the render thread).
	static FScreenPassTexture AddDebugPostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		FNSSTemporalUpscaler* Upscaler = (FNSSTemporalUpscaler*)View.Family->GetTemporalUpscalerInterface();
		// We can directly check the pointers because they should point to the same variable.
		if (Upscaler == nullptr || Upscaler->GetDebugName() != NSSName || !Upscaler->AreDebugTexturesValid)
		{
			FScreenPassTexture OutputTexture = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			return OutputTexture;
		}

		const FScreenPassTextureSlice& SceneColor = Inputs.Textures[(uint32)EPostProcessMaterialInput::SceneColor];
		FIntPoint PaddedLowResViewSize = FIntPoint(
			Upscaler->GetNSSModel()->ModelInstance->GetOutputTensorShapes()[0].GetData()[2],
			Upscaler->GetNSSModel()->ModelInstance->GetOutputTensorShapes()[0].GetData()[1]);

		FScreenPassRenderTarget Output;
		if (Inputs.OverrideOutput.IsValid())
		{
			// If we were told to draw to a specific texture, then first draw the scene colour to it as a background.
			Output = Inputs.OverrideOutput;
			AddDrawTexturePass(GraphBuilder, View, FScreenPassTexture(SceneColor), Output);
			Output.LoadAction = ERenderTargetLoadAction::ELoad; // We want to add to the existing contents.
		}
		else
		{
			// Otherwise just use the scene color and draw our stuff on top.
			Output = FScreenPassRenderTarget(FScreenPassTexture(SceneColor), ERenderTargetLoadAction::ELoad);
		}

		bool SingleTileOnly = false;
		int32 SingleTileIdx = -1;
		if (CVarNSSDebug.GetValueOnRenderThread() >= 3)
		{
			SingleTileOnly = true;
			SingleTileIdx = CVarNSSDebug.GetValueOnRenderThread() - 3;
		}

		const int32 NumTiles1D = SingleTileOnly ? 1 : 4;
		const int32 TilePadding = 10; // Leave some padding between tiles so that you can see (roughly) what's going on behind.
		const int32 TileWidth = (Output.ViewRect.Width() - TilePadding * (NumTiles1D - 1)) / NumTiles1D;
		const int32 TileHeight = (Output.ViewRect.Height() - TilePadding * (NumTiles1D - 1)) / NumTiles1D;
		int32 NextTileIdx = 0;
		FIntPoint NextTilePos = FIntPoint(0, 0);
		bool IsQuantized = Upscaler->DebugPreprocessedBuffer->Desc.BytesPerElement == 1;

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		auto MoveToNextTile = [&]() {
			++NextTileIdx;
			if (SingleTileOnly)
			{
				return;
			}

			// Figure out where the next tile will go. Proceed in rows, but skip out the middle section of the screen to avoid covering it up too much.
			NextTilePos.X += TileWidth + TilePadding;
			if (NextTilePos.X >= Output.ViewRect.Width())
			{
				NextTilePos.X = 0;
				NextTilePos.Y += TileHeight + TilePadding;
			}
			else if (FMath::IsWithin(NextTilePos.Y, Output.ViewRect.Height() / 4, Output.ViewRect.Height() / 4 * 3))
			{
				NextTilePos.X += (TileWidth + TilePadding) * 2;
			}
			};

		auto DrawTileLabel = [&](FString Label) {
			AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("NSS debug labels"), View, Output, [Label = MoveTemp(Label), NextTilePos](FCanvas& Canvas) {
				Canvas.SetBaseTransform(FMatrix(FScaleMatrix(Canvas.GetDPIScale()) * Canvas.CalcBaseTransform2D(Canvas.GetViewRect().Width(), Canvas.GetViewRect().Height())));
				const float DPIScale = Canvas.GetDPIScale();
				Canvas.DrawShadowedString(NextTilePos.X / DPIScale, NextTilePos.Y / DPIScale, *Label, GetStatsFont(), FLinearColor(1, 1, 0));
				});
			};

		auto DrawTileFrom3DBuffer = [&](FRDGBufferRef Buffer, FUintVector3 BufferSizeXYZ, EPixelFormat Format, uint32 FirstChannel, uint32 NumChannels, FString Label) {
			if (!SingleTileOnly || SingleTileIdx == NextTileIdx)
			{
				FNSSDebugVisualizeBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNSSDebugVisualizeBufferPS::FParameters>();
				PassParameters->InBuffer = GraphBuilder.CreateSRV(Buffer, Format);
				PassParameters->BufferSizeXYZ = BufferSizeXYZ;
				PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
				PassParameters->FirstChannel = FirstChannel;
				PassParameters->NumChannels = NumChannels;

				FScreenPassTextureViewport OutputViewport(Output);
				OutputViewport.Rect.Min = NextTilePos;
				OutputViewport.Rect.Max = NextTilePos + FIntPoint(TileWidth, TileHeight);

				FScreenPassTextureViewport InputViewport(FIntPoint(BufferSizeXYZ.X, BufferSizeXYZ.Y));

				FNSSDebugVisualizeBufferPS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FNSSDebugVisualizeBufferPS::FQuantized>(IsQuantized);

				TShaderMapRef<FNSSDebugVisualizeBufferPS> PixelShader(ShaderMap, PermutationVector);
				AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("NSS debug tile: %s", *Label), View, OutputViewport, InputViewport, PixelShader, PassParameters);

				DrawTileLabel(Label);
			}
			MoveToNextTile();
			};

		auto DrawTileFromDepthOffsetTexture = [&](FRDGTextureRef Texture, FIntRect TextureViewRect, FString Label) {
			if (!SingleTileOnly || SingleTileIdx == NextTileIdx)
			{
				TShaderMapRef<FNSSDebugVisualizeDepthOffsetTexturePS> PixelShader(ShaderMap);
				FNSSDebugVisualizeDepthOffsetTexturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNSSDebugVisualizeDepthOffsetTexturePS::FParameters>();
				PassParameters->InputDepthOffsetTexture = Texture;
				PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
				PassParameters->InputDepthOffsetTextureSize = TextureViewRect.Size();

				FScreenPassTextureViewport OutputViewport(Output);
				OutputViewport.Rect.Min = NextTilePos;
				OutputViewport.Rect.Max = NextTilePos + FIntPoint(TileWidth, TileHeight);

				FScreenPassTextureViewport InputViewport(Texture);
				InputViewport.Rect = TextureViewRect;

				AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("NSS debug tile: %s", *Label), View, OutputViewport, InputViewport, PixelShader, PassParameters);

				DrawTileLabel(Label);
			}
			MoveToNextTile();
			};

		auto DrawTileFromPreprocessedBuffer = [&](uint32 FirstChannel, uint32 NumChannels, FString Label) {
			EPixelFormat PreprocessedBufferFormat = IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT;
			DrawTileFrom3DBuffer(Upscaler->DebugPreprocessedBuffer, FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 12), PreprocessedBufferFormat, FirstChannel, NumChannels, Label);
			};

		if (CVarNSSDebug.GetValueOnRenderThread() >= 2)
		{
			DrawTileFromPreprocessedBuffer(0, 3, TEXT("DownsampledTonemappedWarpedPrevFrameUpscaledColour"));
			DrawTileFromPreprocessedBuffer(3, 3, TEXT("JitteredTonemappedInput"));
			DrawTileFromPreprocessedBuffer(6, 1, TEXT("DisocclusionMask"));
			DrawTileFromPreprocessedBuffer(7, 4, TEXT("WarpedFeedback"));
			DrawTileFromPreprocessedBuffer(11, 1, TEXT("LumaDerivative"));
			DrawTileFromDepthOffsetTexture(Upscaler->DebugClosestDepthOffset, FIntRect(FIntPoint::ZeroValue, PaddedLowResViewSize), TEXT("ClosestDepthOffset"));
			DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.KPNFilterCol3], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 4, IsQuantized ? TEXT("KPNFilterCol3 (Quantized 8-bit)") : TEXT("KPNFilterCol3 (float32)"));
			DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.KPNFilterCol2], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 4, IsQuantized ? TEXT("KPNFilterCol2 (Quantized 8-bit)") : TEXT("KPNFilterCol2 (float32)"));
			DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.KPNFilterCol1], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 4, IsQuantized ? TEXT("KPNFilterCol1 (Quantized 8-bit)") : TEXT("KPNFilterCol1 (float32)"));
			DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.KPNFilterCol0], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 4, IsQuantized ? TEXT("KPNFilterCol0 (Quantized 8-bit)") : TEXT("KPNFilterCol0 (float32)"));
		}
		DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.Feedback], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 4, IsQuantized ? TEXT("Feedback (Quantized 8-bit)") : TEXT("Feedback (float32)"));
		DrawTileFrom3DBuffer(Upscaler->DebugNetworkOutputBuffers[Upscaler->GetNSSModel()->OutputIndices.ThetaAlpha], FUintVector3(PaddedLowResViewSize.X, PaddedLowResViewSize.Y, 4), IsQuantized ? EPixelFormat::PF_R32_UINT : EPixelFormat::PF_R32_FLOAT, 0, 2, IsQuantized ? TEXT("ThetaAlpha (Quantized 8-bit)") : TEXT("ThetaAlpha (float32)"));

		Upscaler->AreDebugTexturesValid = false;

		return Output;
	}

	TSharedPtr<NSSModel> NSSModel;
	bool SkipHistoryNextFrame = false;
};

void FNSSModule::StartupModule()
{
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NSS"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/NSS"), PluginShaderDir);

	// We can't register the SceneViewExtension yet, as the Engine hasn't been initialized yet.
	// Register a callback so that we do it later.
	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNSSModule::OnPostEngineInit);
}

void FNSSModule::ShutdownModule()
{
	SceneViewExtension = nullptr;

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "NSS");
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.Remove(OnAssetReimportHandle);
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPropertyChangedHandle);
#endif
	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);
}

void FNSSModule::OnPostEngineInit()
{
#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "NSS",
			LOCTEXT("SettingsName", "NSS"),
			LOCTEXT("SettingsDescription", "Configure the NSS plugin."),
			GetMutableDefault<UNSSSettings>());
	}
#endif

	SceneViewExtension = FSceneViewExtensions::NewExtension<FNSSSceneViewExtension>();

	// Register callback so that we can update the model if the asset is reimported in the editor.
#if WITH_EDITOR
	if (GEditor)
	{
		OnAssetReimportHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddRaw(this, &FNSSModule::OnAssetReimport);
	}
	OnObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FNSSModule::OnObjectPropertyChanged);
#endif
}

void FNSSModule::OnAssetReimport(UObject* Obj)
{
	if (Obj == GetDefault<UNSSSettings>()->NSSModelData)
	{
		SceneViewExtension->RecreateModelFromAsset();
	}
}

void FNSSModule::OnObjectPropertyChanged(UObject* Obj, FPropertyChangedEvent& Event)
{
	if (Obj == GetDefault<UNSSSettings>())
	{
		bool Reload = false;
		if (Event.Property == nullptr)
		{
			Reload = true;
		}
		if (Event.Property != nullptr)
		{
			const FName PropertyName(Event.Property->GetFName());
			if (PropertyName == GET_MEMBER_NAME_CHECKED(UNSSSettings, NSSModelData) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(UNSSSettings, NNERuntime))
			{
				Reload = true;
			}
		}

		if (Reload)
		{
			SceneViewExtension->RecreateModelFromAsset();
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNSSModule, NSS)



