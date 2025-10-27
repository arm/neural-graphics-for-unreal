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

#include "NSSViewExtension.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Materials/Material.h"
#include "NGSettings.h"
#include "NSSModule.h"
#include "NSSProxy.h"
#include "PostProcess/PostProcessing.h"
#include "ScenePrivate.h"

NSSViewExtension::NSSViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	static IConsoleVariable* CVarMinAutomaticViewMipBiasMin =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Min"));
	static IConsoleVariable* CVarMinAutomaticViewMipBiasOffset =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Offset"));

	PreviousNSSState = CVarEnableNSS.GetValueOnAnyThread();
	PreviousNSSStateRT = CVarEnableNSS.GetValueOnAnyThread();
	CurrentNSSStateRT = CVarEnableNSS.GetValueOnAnyThread();
	MinAutomaticViewMipBiasMin = CVarMinAutomaticViewMipBiasMin ? CVarMinAutomaticViewMipBiasMin->GetFloat() : 0;
	MinAutomaticViewMipBiasOffset =
		CVarMinAutomaticViewMipBiasOffset ? CVarMinAutomaticViewMipBiasOffset->GetFloat() : 0;

	INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
	if (NSSModuleInterface.GetTemporalUpscaler() == nullptr)
	{
		NSSModule& NSSModuleInstance = static_cast<NSSModule&>(NSSModuleInterface);
		TSharedPtr<NSS, ESPMode::ThreadSafe> NSSTemporalUpscaler = MakeShared<NSS, ESPMode::ThreadSafe>();
		NSSModuleInstance.SetTemporalUpscaler(NSSTemporalUpscaler);
	}
}

void NSSViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		static IConsoleVariable* CVarMinAutomaticViewMipBiasMin =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Min"));
		static IConsoleVariable* CVarMinAutomaticViewMipBiasOffset =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewTextureMipBias.Offset"));
		INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
		check(NSSModuleInterface.GetNSSUpscaler());

		if (CVarEnableNSS.GetValueOnAnyThread() && !NSSModuleInterface.GetNSSUpscaler()->IsApiSupported())
		{
			NSSModuleInterface.GetNSSUpscaler()->Initialize();

			if (CVarEnableNSS.GetValueOnAnyThread() && NSSModuleInterface.GetNSSUpscaler()->IsApiSupported())
			{
				// Initialize by default for game, but not the editor unless we intend to use NSS in the viewport by
				// default
				if (!GIsEditor || CVarEnableNSSInEditor.GetValueOnAnyThread())
				{
					// Set this at startup so that it will apply consistently
					if (CVarNSSAdjustMipBias.GetValueOnGameThread())
					{
						if (CVarMinAutomaticViewMipBiasMin != nullptr)
						{
							CVarMinAutomaticViewMipBiasMin->Set(float(0.f + log2(1.f / 3.0f) - 1.0f + FLT_EPSILON),
								EConsoleVariableFlags::ECVF_SetByCode);
						}
						if (CVarMinAutomaticViewMipBiasOffset != nullptr)
						{
							CVarMinAutomaticViewMipBiasOffset->Set(
								float(-1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
						}
					}
				}
				else
				{
					// Pretend it is disabled so that when the Editor does enable NSS the state change is picked up
					// properly.
					PreviousNSSState = 0;
					PreviousNSSStateRT = 0;
					CurrentNSSStateRT = 0;
				}
			}
			else
			{
				// Disable NSS as it could not be initialised, this avoids errors if it is enabled later.
				PreviousNSSState = 0;
				PreviousNSSStateRT = 0;
				CurrentNSSStateRT = 0;
				CVarEnableNSS->Set(0, EConsoleVariableFlags::ECVF_SetByConsole);
			}
		}

		int32 EnableNSS = CVarEnableNSS.GetValueOnAnyThread();

		if (PreviousNSSState != EnableNSS)
		{
			// Update tracking of the NSS state when it is changed
			PreviousNSSState = EnableNSS;

			if (EnableNSS)
			{
				// When toggling reapply the settings that NSS wants to override
				if (CVarNSSAdjustMipBias.GetValueOnGameThread())
				{
					if (CVarMinAutomaticViewMipBiasMin != nullptr)
					{
						MinAutomaticViewMipBiasMin = CVarMinAutomaticViewMipBiasMin->GetFloat();
						CVarMinAutomaticViewMipBiasMin->Set(
							float(0.f + log2(1.f / 3.0f) - 1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarMinAutomaticViewMipBiasOffset != nullptr)
					{
						MinAutomaticViewMipBiasOffset = CVarMinAutomaticViewMipBiasOffset->GetFloat();
						CVarMinAutomaticViewMipBiasOffset->Set(
							float(-1.0f + FLT_EPSILON), EConsoleVariableFlags::ECVF_SetByCode);
					}
				}
			}
			// Put the variables NSS modifies back to the way they were when NSS was toggled on.
			else
			{
				if (CVarNSSAdjustMipBias.GetValueOnGameThread())
				{
					if (CVarMinAutomaticViewMipBiasMin != nullptr)
					{
						CVarMinAutomaticViewMipBiasMin->Set(
							MinAutomaticViewMipBiasMin, EConsoleVariableFlags::ECVF_SetByCode);
					}
					if (CVarMinAutomaticViewMipBiasOffset != nullptr)
					{
						CVarMinAutomaticViewMipBiasOffset->Set(
							MinAutomaticViewMipBiasOffset, EConsoleVariableFlags::ECVF_SetByCode);
					}
				}
			}
		}
	}
}

void NSSViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
		NSS* Upscaler = NSSModuleInterface.GetNSSUpscaler();
		bool IsTemporalUpscalingRequested = false;
		bool bIsGameView = !WITH_EDITOR;
		for (int i = 0; i < InViewFamily.Views.Num(); i++)
		{
			const FSceneView* InView = InViewFamily.Views[i];
			if (ensure(InView))
			{
				bIsGameView |= InView->bIsGameView;

				// Don't run NSS if Temporal Upscaling is unused.
				IsTemporalUpscalingRequested |=
					(InView->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale);
			}
		}

#if WITH_EDITOR
		IsTemporalUpscalingRequested &= Upscaler->IsEnabledInEditor();
#endif

		if (IsTemporalUpscalingRequested && CVarEnableNSS.GetValueOnAnyThread()
			&& (InViewFamily.GetTemporalUpscalerInterface() == nullptr))
		{
			if (!WITH_EDITOR || (CVarEnableNSSInEditor.GetValueOnGameThread() == 1) || bIsGameView)
			{
				Upscaler->UpdateDynamicResolutionState();
				InViewFamily.SetTemporalUpscalerInterface(new NSSProxy(Upscaler));
			}
		}
	}
}

void NSSViewExtension::PreRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		// When the NSS plugin is enabled/disabled dispose of any previous history as it will be invalid if it comes
		// from another upscaler
		CurrentNSSStateRT = CVarEnableNSS.GetValueOnRenderThread();
		if (PreviousNSSStateRT != CurrentNSSStateRT)
		{
			// This also requires updating our tracking of the NSS state
			PreviousNSSStateRT = CurrentNSSStateRT;
		}
	}
}

void NSSViewExtension::PreRenderView_RenderThread(FRenderGraphType& GraphBuilder, FSceneView& InView)
{
	// NSS can access the previous frame of Lumen data at this point, but not later where it will be replaced with the
	// current frame's which won't be accessible when NSS runs.
	if (InView.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		if (CVarEnableNSS.GetValueOnAnyThread())
		{
			INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
			// NSSModuleInterface.GetNSSUpscaler()->SetLumenReflections(InView);
		}
	}
}

void NSSViewExtension::PrePostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	// NSS requires the separate translucency data which is only available through the post-inputs so bind them to the
	// upscaler now.
	if (View.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		if (CVarEnableNSS.GetValueOnAnyThread())
		{
			INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
			NSSModuleInterface.GetNSSUpscaler()->SetPostProcessingInputs(Inputs);
		}
	}
}

void NSSViewExtension::PostRenderViewFamily_RenderThread(FRenderGraphType& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// As NSS retains pointers/references to objects the engine is not expecting clear them out now to prevent leaks or
	// accessing dangling pointers.
	if (InViewFamily.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1)
	{
		if (CVarEnableNSS.GetValueOnAnyThread())
		{
			INSSModule& NSSModuleInterface = FModuleManager::GetModuleChecked<INSSModule>(TEXT("NSS"));
			NSSModuleInterface.GetNSSUpscaler()->EndOfFrame();
		}
	}
}
