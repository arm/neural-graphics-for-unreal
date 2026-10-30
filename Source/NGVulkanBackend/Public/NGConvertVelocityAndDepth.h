// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#pragma once
#include "CoreMinimal.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "FFXRDGBuilder.h"
#include "GlobalShader.h"
#include "RenderGraphFwd.h"
#include "ShaderParameterStruct.h"

// Forward declarations
class FViewInfo;

// Shared pixel-shader conversion for sparse velocity and depth. NFRU can
// select the SDK MV/depth lane extent while NSS keeps its velocity-only path.
class NGConvertVelocityAndDepth : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(NGConvertVelocityAndDepth);
	SHADER_USE_PARAMETER_STRUCT(NGConvertVelocityAndDepth, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	RDG_TEXTURE_ACCESS(DepthTexture, ERHIAccess::SRVGraphics)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDepth)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputVelocity)
	SHADER_PARAMETER(FIntPoint, ContentSize)
	SHADER_PARAMETER(FVector2f, InvContentSize)
	SHADER_PARAMETER(FVector2f, SourceToOutputRatio)
	SHADER_PARAMETER(FIntPoint, SourceRectMin)
	SHADER_PARAMETER(uint32, InvertedDepth)
	SHADER_PARAMETER(uint32, NeedsDepthSearch)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

struct FNGConvertVelocityResult
{
	FRDGTextureRef Velocity = nullptr;
	FRDGTextureRef Depth = nullptr;
};

// Converts sparse velocity and depth into the shared MRT representation.
NGVULKANBACKEND_API FNGConvertVelocityResult helper_NGConvertVelocityAndDepth(FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef SceneVelocity,
	FIntRect ViewRect,
	const FViewInfo* View,
	const bool bNeedsUAV,
	FIntPoint OutputExtent,
	FRDGTextureRef OutputDepth = nullptr);
