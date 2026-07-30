
// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#include "NGConvertVelocity.h"
#include "PixelShaderUtils.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

IMPLEMENT_GLOBAL_SHADER(NGConvertVelocity, "/Plugin/NG/Private/NGConvertVelocity.usf", "main", SF_Pixel);

void helper_NGConvertVelocity(FRDGBuilder& GraphBuilder,
	FRDGTextureRef MotionVectorTexture,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef SceneVelocity,
	FIntRect ViewRect,
	const FViewInfo* View)
{
	NGConvertVelocity::FParameters* PassParameters = GraphBuilder.AllocParameters<NGConvertVelocity::FParameters>();

	FRDGTextureSRVDesc DepthDesc = FRDGTextureSRVDesc::Create(SceneDepth);
	FRDGTextureSRVDesc VelocityDesc = FRDGTextureSRVDesc::Create(SceneVelocity);

	PassParameters->DepthTexture = SceneDepth;
	PassParameters->InputDepth = GraphBuilder.CreateSRV(DepthDesc);
	PassParameters->InputVelocity = GraphBuilder.CreateSRV(VelocityDesc);
	FIntPoint Size = ViewRect.Size();
	PassParameters->InvContentSize = FVector2f(1.0f / float(Size.X), 1.0f / float(Size.Y));
	PassParameters->View = View->ViewUniformBuffer;

	const FScreenPassRenderTarget MotionVectorNewRT(MotionVectorTexture, ViewRect, ERenderTargetLoadAction::ENoAction);

	PassParameters->RenderTargets[0] = MotionVectorNewRT.GetRenderTargetBinding();

	TShaderMapRef<NGConvertVelocity> ConvertVelocityShader(View->ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
		View->ShaderMap,
		RDG_EVENT_NAME("NG ConvertVelocity (PS)"),
		ConvertVelocityShader,
		PassParameters,
		ViewRect);
}
