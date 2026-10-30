// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
// Console variables for both NSS and NFRU.
//------------------------------------------------------------------------------------------------------
// clang-format off
TAutoConsoleVariable<int32> CVarNGDeferDelete(
	TEXT("r.NG.DeferDelete"),
	5, // Empirical data
	TEXT("Number of frames to defer deletion. Must be greater than 0."),
	ECVF_RenderThreadSafe);

//------------------------------------------------------------------------------------------------------
// Console variables that control how NSS operates.
//------------------------------------------------------------------------------------------------------
TAutoConsoleVariable<int32> CVarEnableNSS(
	TEXT("r.NSS.Enable"),
	0,
	TEXT("Enable NG Neural Super Sampling for Temporal Upscale."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSUseFragmentShader(
	TEXT("r.NSS.UseFragmentShader"),
	1,
	TEXT("Using fragment job for NSS."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSShaderQualityMode(
	TEXT("r.NSS.ShaderQualityMode"),
	1,
	TEXT("Shader quality mode for NSS."
		"\n0: quality (high), 1: balanced (mid), 2: performance (low)."
		"\nDefault is 1 (balanced)."
	),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarEnableNSSInEditor(
	TEXT("r.NSS.EnableInEditorViewport"),
	0,
	TEXT("Enable NG Neural Super Sampling for Temporal Upscale in the editor viewport."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSDebug(
	TEXT("r.NSS.Debug"),
	0,
	TEXT("Turn on the debug views (0 = off, 1 = on). "
		 "\nThe views are:"
		 "\n- Row 0: [history_color | input_depth | prev_depth | nearest_offset]"
		 "\n- Row 1: [low_res_color | motion_vector | luma_deriv_tm1 | temporal_feedback]"
		 "\n- Row 2: [lr_warped_history | disocclusion_mask | luma_deriv_t | depth_dilated]"
		 "\n- Row 3: [unjittered_color | motion_detector | luma_instability | warp_feedback]"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSDebugViewMode(
	TEXT("r.NSS.DebugViewMode"),
	0,
	TEXT("To control the debug view tiles. Only works when r.NSS.Debug is enabled."
		 "\nCheck the sdk NSS.md for detailed debug view tiles."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNSSAdjustMipBias(
	TEXT("r.NSS.AdjustMipBias"),
	1,
	TEXT("Allow NSS to adjust the minimum global texture mip bias "
		 "(r.ViewTextureMipBias.Min & r.ViewTextureMipBias.Offset)"),
	ECVF_ReadOnly);

//------------------------------------------------------------------------------------------------------
// Console variables for Frame Interpolation.
//------------------------------------------------------------------------------------------------------

TAutoConsoleVariable<int32> CVarEnableNFRU(TEXT("r.NFRU.Enable"),
	0,
	TEXT("Enable Neural Frame Rate Upscaling"),
	ECVF_RenderThreadSafe);

// UE_BUILD_SHIPPING will be 0, in non-shipping builds can experiment with enabling this to capture debug UI.
TAutoConsoleVariable<int32> CVarNFRUCaptureDebugUI(TEXT("r.NFRU.CaptureDebugUI"),
	!UE_BUILD_SHIPPING,
	TEXT(
		"Force Neural Frame Rate Upscaling to detect and copy any debug UI which only renders on the first invocation of Slate's DrawWindow command."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUUpdateGlobalFrameTime(TEXT("r.NFRU.UpdateGlobalFrameTime"),
	0,
	TEXT(
		"Update the GAverageMS and GAverageFPS engine globals with the frame time & FPS including frame interpolation."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUModifySlateDeltaTime(TEXT("r.NFRU.ModifySlateDeltaTime"),
	1,
	TEXT(
		"Set the FSlateApplication delta time to 0.0 when redrawing the UI for the 'Slate Redraw' UI mode to prevent widgets' NativeTick implementations updating incorrectly, ignored when using 'UI Extraction'."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUPaceAdjuster(TEXT("r.NFRU.PaceAdjuster"),
	0,
	TEXT("Enable the FPS Pace Adjuster to dynamically adjust the target FPS based on performance."),
	ECVF_RenderThreadSafe);

// Empirical values: Number of consecutive frames required to trigger FPS adjustment.
// Tune these values according to project performance and user experience.
TAutoConsoleVariable<int32> CVarNFRUUpAdjustFrameCount(TEXT("r.NFRU.UpAdjustFrameCount"),
	40,
	TEXT("Number of consecutive frames required to increase FPS (default: 40)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUDownAdjustFrameCount(TEXT("r.NFRU.DownAdjustFrameCount"),
	20,
	TEXT("Number of consecutive frames required to decrease FPS (default: 20)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUUseFragmentShader(
	TEXT("r.NFRU.UseFragmentShader"),
	1,
	TEXT("Using fragment job for NFRU."),
	ECVF_RenderThreadSafe);

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
TAutoConsoleVariable<int32> CVarNFRUOnlyInterpolatedFrames(TEXT("r.NFRU.OnlyInterpolatedFrames"),
	0,
	TEXT("Present only interpolated frames when running Frame Interpolation."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarNFRUShowDebugView(TEXT("r.NFRU.ShowDebugView"),
	0,
	TEXT("Show the debug view when running Frame Interpolation."),
	ECVF_RenderThreadSafe);
#endif
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

FText UNGSettings::GetSectionText() const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NG"));
	if (!Plugin.IsValid())
	{
		return Super::GetSectionText();
	}

	return FText::Format(LOCTEXT("NGSettingsSectionText", "Neural Graphics {0}"),
		FText::AsCultureInvariant(Plugin->GetDescriptor().VersionName));
}

FText UNGSettings::GetSectionDescription() const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NG"));
	if (!Plugin.IsValid())
	{
		return Super::GetSectionDescription();
	}

	return FText::Format(
		LOCTEXT("NGSettingsSectionDescription", "Settings for Arm Neural Graphics {0} exposed through the Editor UI."),
		FText::AsCultureInvariant(Plugin->GetDescriptor().VersionName));
}

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
