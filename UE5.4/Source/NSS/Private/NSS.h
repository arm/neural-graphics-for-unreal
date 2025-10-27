// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (C) 2023-2025 Advanced Micro Devices, Inc. All rights reserved.
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

#pragma once

#include "Containers/LockFreeList.h"
#include "Engine/Engine.h"
#include "NGSharedBackend.h"
#include "NSSHistory.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/TemporalAA.h"
#include "ScreenSpaceDenoise.h"
#include "Shaders/NssConvertVelocity.h"
#include "Shaders/NssMirrorPad.h"
#include "TemporalUpscaler.h"
using INSS = UE::Renderer::Private::ITemporalUpscaler;
using NSSPassInput = UE::Renderer::Private::ITemporalUpscaler::FInputs;
using NSSView = FSceneView;

#ifndef ENGINE_HAS_DENOISE_INDIRECT
#define ENGINE_HAS_DENOISE_INDIRECT 0
#endif

struct FPostProcessingInputs;

//-------------------------------------------------------------------------------------
// The core upscaler implementation for NSS.
// Implements IScreenSpaceDenoiser in order to access the reflection texture data.
//-------------------------------------------------------------------------------------
class NSS final : public INSS, public IScreenSpaceDenoiser
{
public:
	NSS();
	virtual ~NSS();

	void Initialize() const;

	const TCHAR* GetDebugName() const override;

	void ReleaseState(NSSStateRef State);

	// Keeping this in for the purposes of checking that everything is initialized correctly.
	static class INGSharedBackend* GetApiAccessor(EFFXBackendAPI& Api);

#if DO_CHECK || DO_GUARD_SLOW || DO_ENSURE
	static void OnNSSMessage(uint32 type, const wchar_t* message);
#endif

	static void SaveScreenPercentage();
	static void UpdateScreenPercentage();
	static void RestoreScreenPercentage();

	static void OnChangeNSSEnable(IConsoleVariable* Var);
	static void OnChangeScreenPercentage(IConsoleVariable* Var);

	class FRDGBuilder* GetGraphBuilder();

	INSS::FOutputs AddPasses(
		FRDGBuilder& GraphBuilder, const NSSView& View, const NSSPassInput& PassInputs) const override;

	INSS* Fork_GameThread(const class FSceneViewFamily& InViewFamily) const override;

	float GetMinUpsampleResolutionFraction() const override;
	float GetMaxUpsampleResolutionFraction() const override;

	void SetPostProcessingInputs(FPostProcessingInputs const& Inputs);

	void EndOfFrame();

	void UpdateDynamicResolutionState();

#if WITH_EDITOR
	bool IsEnabledInEditor() const;
	void SetEnabledInEditor(bool bEnabled);
#endif

	FReflectionsOutputs DenoiseReflections(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FReflectionsInputs& ReflectionInputs,
		const FReflectionsRayTracingConfig RayTracingConfig) const override;

	EShadowRequirements GetShadowRequirements(const FViewInfo& View,
		const FLightSceneInfo& LightSceneInfo,
		const FShadowRayTracingConfig& RayTracingConfig) const override;

	void DenoiseShadowVisibilityMasks(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const TStaticArray<FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize>& InputParameters,
		const int32 InputParameterCount,
		TStaticArray<FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize>& Outputs) const override;

	FPolychromaticPenumbraOutputs DenoisePolychromaticPenumbraHarmonics(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FPolychromaticPenumbraHarmonics& Inputs) const override;

	FReflectionsOutputs DenoiseWaterReflections(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FReflectionsInputs& ReflectionInputs,
		const FReflectionsRayTracingConfig RayTracingConfig) const override;

	FAmbientOcclusionOutputs DenoiseAmbientOcclusion(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FAmbientOcclusionInputs& ReflectionInputs,
		const FAmbientOcclusionRayTracingConfig RayTracingConfig) const override;

	FSSDSignalTextures DenoiseDiffuseIndirect(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FDiffuseIndirectInputs& Inputs,
		const FAmbientOcclusionRayTracingConfig Config) const override;

#if ENGINE_HAS_DENOISE_INDIRECT
	FSSDSignalTextures DenoiseIndirect(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FIndirectInputs& Inputs,
		const FAmbientOcclusionRayTracingConfig Config) const override;
#endif

	FDiffuseIndirectOutputs DenoiseSkyLight(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FDiffuseIndirectInputs& Inputs,
		const FAmbientOcclusionRayTracingConfig Config) const override;

	FSSDSignalTextures DenoiseDiffuseIndirectHarmonic(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FDiffuseIndirectHarmonic& Inputs,
		const HybridIndirectLighting::FCommonParameters& CommonDiffuseParameters) const override;

	bool SupportsScreenSpaceDiffuseIndirectDenoiser(EShaderPlatform Platform) const override;

	FSSDSignalTextures DenoiseScreenSpaceDiffuseIndirect(FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		const FSceneTextureParameters& SceneTextures,
		const FDiffuseIndirectInputs& Inputs,
		const FAmbientOcclusionRayTracingConfig Config) const override;

	inline bool IsApiSupported() const
	{
		return Api != EFFXBackendAPI::Unknown && Api != EFFXBackendAPI::Unsupported;
	}

private:
	void DeferredCleanup(uint64 FrameNum) const;

	mutable FPostProcessingInputs PostInputs;
	FDynamicResolutionStateInfos DynamicResolutionStateInfos;
	mutable FCriticalSection Mutex;
	// For handling of FFX NSS States while it is temporarily turned off
	// - avoids reallocating stuff and just keeps it around
	// Will we ever need this? Will we have camera cuts without NSS?
	mutable TSet<NSSStateRef> AvailableStates;
	mutable EFFXBackendAPI Api;
	mutable class INGSharedBackend* ApiAccessor;
	mutable class FRDGBuilder* CurrentGraphBuilder;
	mutable const IScreenSpaceDenoiser* WrappedDenoiser;
	mutable TRefCountPtr<IPooledRenderTarget> MotionVectorRT;
#if WITH_EDITOR
	bool bEnabledInEditor;
#endif
	static float SavedScreenPercentage;
};
