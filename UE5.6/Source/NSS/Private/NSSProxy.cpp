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

#include "NSSProxy.h"

#include "FXSystem.h"
#include "LegacyScreenPercentageDriver.h"
#include "LogNSS.h"
#include "NSS.h"
#include "NSSHistory.h"
#include "NSSInclude.h"
#include "PlanarReflectionSceneProxy.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceRayTracing.h"
#include "Serialization/MemoryImage.h"
#include "Serialization/MemoryLayout.h"
#include "TranslucentRendering.h"

//------------------------------------------------------------------------------------------------------
// NSSProxy implementation.
//------------------------------------------------------------------------------------------------------
NSSProxy::NSSProxy(NSS* TemporalUpscaler) : TemporalUpscaler(TemporalUpscaler)
{
	check(TemporalUpscaler);
}

NSSProxy::~NSSProxy() {}

const TCHAR* NSSProxy::GetDebugName() const
{
	return TemporalUpscaler->GetDebugName();
}

INSS::FOutputs NSSProxy::AddPasses(FRDGBuilder& GraphBuilder, const NSSView& View, const NSSPassInput& PassInputs) const
{
	return TemporalUpscaler->AddPasses(GraphBuilder, View, PassInputs);
}

INSS* NSSProxy::Fork_GameThread(const class FSceneViewFamily& InViewFamily) const
{
	return new NSSProxy(TemporalUpscaler);
}

float NSSProxy::GetMinUpsampleResolutionFraction() const
{
	return TemporalUpscaler->GetMinUpsampleResolutionFraction();
}

float NSSProxy::GetMaxUpsampleResolutionFraction() const
{
	return TemporalUpscaler->GetMinUpsampleResolutionFraction();
}

IScreenSpaceDenoiser::FReflectionsOutputs NSSProxy::DenoiseReflections(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseReflections(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

IScreenSpaceDenoiser::EShadowRequirements NSSProxy::GetShadowRequirements(
	const FViewInfo& View, const FLightSceneInfo& LightSceneInfo, const FShadowRayTracingConfig& RayTracingConfig) const
{
	return TemporalUpscaler->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
}

void NSSProxy::DenoiseShadowVisibilityMasks(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const TStaticArray<FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize>& InputParameters,
	const int32 InputParameterCount,
	TStaticArray<FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize>& Outputs) const
{
	return TemporalUpscaler->DenoiseShadowVisibilityMasks(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, InputParameters, InputParameterCount, Outputs);
}

IScreenSpaceDenoiser::FPolychromaticPenumbraOutputs NSSProxy::DenoisePolychromaticPenumbraHarmonics(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FPolychromaticPenumbraHarmonics& Inputs) const
{
	return TemporalUpscaler->DenoisePolychromaticPenumbraHarmonics(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs);
}

IScreenSpaceDenoiser::FReflectionsOutputs NSSProxy::DenoiseWaterReflections(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FReflectionsInputs& ReflectionInputs,
	const FReflectionsRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseWaterReflections(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

IScreenSpaceDenoiser::FAmbientOcclusionOutputs NSSProxy::DenoiseAmbientOcclusion(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FAmbientOcclusionInputs& ReflectionInputs,
	const FAmbientOcclusionRayTracingConfig RayTracingConfig) const
{
	return TemporalUpscaler->DenoiseAmbientOcclusion(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, ReflectionInputs, RayTracingConfig);
}

FSSDSignalTextures NSSProxy::DenoiseDiffuseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseDiffuseIndirect(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

#if ENGINE_HAS_DENOISE_INDIRECT
FSSDSignalTextures NSSProxy::DenoiseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseIndirect(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
#endif

IScreenSpaceDenoiser::FDiffuseIndirectOutputs NSSProxy::DenoiseSkyLight(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseSkyLight(GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}

FSSDSignalTextures NSSProxy::DenoiseDiffuseIndirectHarmonic(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectHarmonic& Inputs,
	const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters) const

{
	return TemporalUpscaler->DenoiseDiffuseIndirectHarmonic(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, CommonDiffuseParameters);
}

bool NSSProxy::SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const
{
	return TemporalUpscaler->SupportsScreenSpaceDiffuseIndirectDenoiser(Platform);
}

FSSDSignalTextures NSSProxy::DenoiseScreenSpaceDiffuseIndirect(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FPreviousViewInfo* PreviousViewInfos,
	const FSceneTextureParameters& SceneTextures,
	const FDiffuseIndirectInputs& Inputs,
	const FAmbientOcclusionRayTracingConfig Config) const
{
	return TemporalUpscaler->DenoiseScreenSpaceDiffuseIndirect(
		GraphBuilder, View, PreviousViewInfos, SceneTextures, Inputs, Config);
}
