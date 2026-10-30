
// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#include "NGConvertVelocityAndDepth.h"
#include "PixelShaderUtils.h"
#include "RenderTargetPool.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

IMPLEMENT_GLOBAL_SHADER(
	NGConvertVelocityAndDepth, "/Plugin/NG/Private/NGConvertVelocityAndDepth.usf", "main", SF_Pixel);

FNGConvertVelocityResult helper_NGConvertVelocityAndDepth(FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef SceneVelocity,
	FIntRect ViewRect,
	const FViewInfo* View,
	const bool bNeedsUAV,
	FIntPoint OutputExtent,
	FRDGTextureRef OutputDepth)
{
	FNGConvertVelocityResult Result;
	const FIntPoint ContentSize = ViewRect.Size();
	if (OutputExtent.X <= 0 || OutputExtent.Y <= 0 || ContentSize.X <= 0 || ContentSize.Y <= 0)
	{
		return Result;
	}

	TRefCountPtr<IPooledRenderTarget> PooledVelocityTarget;
	TRefCountPtr<IPooledRenderTarget> PooledDepthTarget;
	const ETextureCreateFlags ConversionTextureFlags =
		TexCreate_ShaderResource | TexCreate_RenderTargetable | (bNeedsUAV ? TexCreate_UAV : TexCreate_None);
	auto CreateConversionTarget =
		[&](EPixelFormat Format, const TCHAR* Name, TRefCountPtr<IPooledRenderTarget>& PooledTarget) -> FRDGTextureRef
	{
		const FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(OutputExtent,
			Format,
			FClearValueBinding::Transparent,
			ConversionTextureFlags,
			ConversionTextureFlags,
			false));
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, PooledTarget, Name);
		return PooledTarget.IsValid() ? GraphBuilder.RegisterExternalTexture(PooledTarget) : nullptr;
	};

	FRDGTextureRef VelocityTexture =
		CreateConversionTarget(PF_G16R16F, TEXT("ConvertedVelocity"), PooledVelocityTarget);
	if (!VelocityTexture)
	{
		return Result;
	}

	FRDGTextureRef ConvertedDepth = OutputDepth;
	if (!ConvertedDepth)
	{
		ConvertedDepth = CreateConversionTarget(PF_R32_FLOAT, TEXT("ConvertedDepth"), PooledDepthTarget);
		if (!ConvertedDepth)
		{
			return Result;
		}
	}

	NGConvertVelocityAndDepth::FParameters* PassParameters =
		GraphBuilder.AllocParameters<NGConvertVelocityAndDepth::FParameters>();
	FRDGTextureSRVDesc InputDepthDesc = FRDGTextureSRVDesc::Create(SceneDepth);
	FRDGTextureSRVDesc InputVelocityDesc = FRDGTextureSRVDesc::Create(SceneVelocity);
	PassParameters->DepthTexture = SceneDepth;
	PassParameters->InputDepth = GraphBuilder.CreateSRV(InputDepthDesc);
	PassParameters->InputVelocity = GraphBuilder.CreateSRV(InputVelocityDesc);
	PassParameters->ContentSize = ContentSize;
	PassParameters->InvContentSize = FVector2f(1.0f / float(ContentSize.X), 1.0f / float(ContentSize.Y));
	PassParameters->SourceToOutputRatio =
		FVector2f(float(ContentSize.X) / float(OutputExtent.X), float(ContentSize.Y) / float(OutputExtent.Y));
	PassParameters->SourceRectMin = ViewRect.Min;
	PassParameters->InvertedDepth = bool(ERHIZBuffer::IsInverted) ? 1u : 0u;
	PassParameters->NeedsDepthSearch = (ContentSize.X != OutputExtent.X || ContentSize.Y != OutputExtent.Y) ? 1u : 0u;
	PassParameters->View = View->ViewUniformBuffer;

	const FIntRect OutputRect(FIntPoint::ZeroValue, OutputExtent);
	const FScreenPassRenderTarget VelocityRT(VelocityTexture, OutputRect, ERenderTargetLoadAction::ENoAction);
	const FScreenPassRenderTarget DepthRT(ConvertedDepth, OutputRect, ERenderTargetLoadAction::ENoAction);
	PassParameters->RenderTargets[0] = VelocityRT.GetRenderTargetBinding();
	PassParameters->RenderTargets[1] = DepthRT.GetRenderTargetBinding();

	TShaderMapRef<NGConvertVelocityAndDepth> ConvertVelocityShader(View->ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
		View->ShaderMap,
		RDG_EVENT_NAME("NG ConvertVelocityAndDepth (PS)"),
		ConvertVelocityShader,
		PassParameters,
		OutputRect);

	Result.Velocity = VelocityTexture;
	Result.Depth = ConvertedDepth;
	return Result;
}
