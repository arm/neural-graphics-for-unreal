
// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#include "NGConvertVelocity.h"
#include "PixelShaderUtils.h"
#include "RenderTargetPool.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

IMPLEMENT_GLOBAL_SHADER(NGConvertVelocity, "/Plugin/NG/Private/NGConvertVelocity.usf", "main", SF_Pixel);

FRDGTextureRef helper_NGConvertVelocity(FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef SceneVelocity,
	FIntRect ViewRect,
	const FViewInfo* View,
	bool bNeedsUAV)
{
	FNGConvertVelocityCache& Cache = GraphBuilder.Blackboard.Create<FNGConvertVelocityCache>();
	FNGConvertVelocityCacheEntry& Entry = Cache.Entries.AddDefaulted_GetRef();
	Entry.InputDepth = SceneDepth;
	Entry.ViewRect = ViewRect;
	Entry.View = View;
	Entry.bIsNSSVelocityCacheEntry = true;

	const ETextureCreateFlags MotionVectorFlags =
		TexCreate_ShaderResource | TexCreate_RenderTargetable | (bNeedsUAV ? TexCreate_UAV : TexCreate_None);
	const FPooledRenderTargetDesc MotionVectorDesc(FPooledRenderTargetDesc::Create2DDesc(SceneVelocity->Desc.Extent,
		PF_G16R16F,
		FClearValueBinding::Transparent,
		MotionVectorFlags,
		MotionVectorFlags,
		false));
	GRenderTargetPool.FindFreeElement(
		GraphBuilder.RHICmdList, MotionVectorDesc, Entry.PooledTarget, TEXT("NGConvertedVelocity"));
	FRDGTextureRef MotionVectorTexture = GraphBuilder.RegisterExternalTexture(Entry.PooledTarget);
	NGConvertVelocity::FParameters* PassParameters = GraphBuilder.AllocParameters<NGConvertVelocity::FParameters>();

	FRDGTextureSRVDesc DepthDesc = FRDGTextureSRVDesc::Create(SceneDepth);
	FRDGTextureSRVDesc VelocityDesc = FRDGTextureSRVDesc::Create(SceneVelocity);

	PassParameters->DepthTexture = SceneDepth;
	PassParameters->InputDepth = GraphBuilder.CreateSRV(DepthDesc);
	PassParameters->InputVelocity = GraphBuilder.CreateSRV(VelocityDesc);
	const FIntPoint ContentSize = ViewRect.Size();
	PassParameters->ContentSize = ContentSize;
	PassParameters->InvContentSize = FVector2f(1.0f / float(ContentSize.X), 1.0f / float(ContentSize.Y));
	PassParameters->InvOutputExtent = PassParameters->InvContentSize;
	PassParameters->View = View->ViewUniformBuffer;

	// NSS supplies zero-origin, view-sized textures. The pass must therefore also
	// render from (0, 0); using the original viewport rect here would apply the
	// offset a second time.
	const FIntRect OutputRect(FIntPoint::ZeroValue, SceneVelocity->Desc.Extent);
	const FScreenPassRenderTarget MotionVectorNewRT(
		MotionVectorTexture, OutputRect, ERenderTargetLoadAction::ENoAction);

	PassParameters->RenderTargets[0] = MotionVectorNewRT.GetRenderTargetBinding();

	TShaderMapRef<NGConvertVelocity> ConvertVelocityShader(View->ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder,
		View->ShaderMap,
		RDG_EVENT_NAME("NG ConvertVelocity (PS)"),
		ConvertVelocityShader,
		PassParameters,
		OutputRect);

	Entry.ConvertedVelocity = MotionVectorTexture;
	return MotionVectorTexture;
}

FNGCachedNSSInputs helper_NGGetCachedNSSInputs(FRDGBuilder& GraphBuilder, FIntRect ViewRect, const FViewInfo* View)
{
	FNGCachedNSSInputs Result;
	const FNGConvertVelocityCache* Cache = GraphBuilder.Blackboard.Get<FNGConvertVelocityCache>();
	if (!Cache)
	{
		return Result;
	}
	for (const FNGConvertVelocityCacheEntry& Entry : Cache->Entries)
	{
		if (Entry.ViewRect == ViewRect && Entry.View == View && Entry.bIsNSSVelocityCacheEntry
			&& Entry.ConvertedVelocity)
		{
			Result.Velocity = Entry.ConvertedVelocity;
			Result.Depth = Entry.InputDepth;
			return Result;
		}
	}
	return Result;
}
