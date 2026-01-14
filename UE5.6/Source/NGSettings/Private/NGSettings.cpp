// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "NGSettings.h"

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigUtilities.h"
#include "Misc/EngineVersionComparison.h"

#define LOCTEXT_NAMESPACE "NGSettingsModule"

IMPLEMENT_MODULE(NGSettingsModule, NGSettings)

//------------------------------------------------------------------------------------------------------
// Console variables that control how NSS operates.
//------------------------------------------------------------------------------------------------------
// clang-format off
TAutoConsoleVariable<int32> CVarEnableNSS(
	TEXT("r.NSS.Enable"),
	0,
	TEXT("Enable ArmNG Neural Super Sampling for Temporal Upscale."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarEnableNSSInEditor(
	TEXT("r.NSS.EnableInEditorViewport"),
	0,
	TEXT("Enable ArmNG Neural Super Sampling for Temporal Upscale in the editor viewport."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSDebug(
	TEXT("r.NSS.Debug"),
	0,
	TEXT("Turn on the debug views (0 = off, 1 = on). "
		 "\nThe views are: "
		 "\nTop row: 1. Final Upscaled Image, 2. UnjitteredTonemappedInput, 3. "
		 "DownsampledTonemappedWarpedPrevFrameUpscaledColor, "
		 "\nMiddle row: 4. Weights tensor, 5. Feedback tensor, 6. Disocclusion Mask, "
		 "\nBottom row: 7. Motion Length, 8. WarpedPrevFrameThetaAlpha, 9. RescaledLumaDerivative."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSAdjustMipBias(
	TEXT("r.NSS.AdjustMipBias"),
	1,
	TEXT("Allow NSS to adjust the minimum global texture mip bias "
		 "(r.ViewTextureMipBias.Min & r.ViewTextureMipBias.Offset)"),
	ECVF_ReadOnly);
// clang-format on

//-------------------------------------------------------------------------------------
// NGSettingsModule
//-------------------------------------------------------------------------------------
void NGSettingsModule::StartupModule()
{
	UE::ConfigUtilities::ApplyCVarSettingsFromIni(
		TEXT("/Script/NGSettings.NGSettings"), *GEngineIni, ECVF_SetByProjectSetting);
}

void NGSettingsModule::ShutdownModule() {}

//-------------------------------------------------------------------------------------
// UNGSettings
//-------------------------------------------------------------------------------------
FName UNGSettings::GetContainerName() const
{
	static const FName ContainerName("Project");
	return ContainerName;
}

FName UNGSettings::GetCategoryName() const
{
	static const FName EditorCategoryName("Plugins");
	return EditorCategoryName;
}

FName UNGSettings::GetSectionName() const
{
	static const FName EditorSectionName("NGSettings");
	return EditorSectionName;
}

void UNGSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif
}

#if WITH_EDITOR

void UNGSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);
	}
}

#endif

#undef LOCTEXT_NAMESPACE
