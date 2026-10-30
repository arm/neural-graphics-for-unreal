// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

// clang-format off
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "NGSettings.generated.h"
// clang-format on

UENUM(BlueprintType)
enum class ENSSShaderQualityMode : uint8
{
	Quality UMETA(DisplayName = "Quality"),
	Balanced UMETA(DisplayName = "Balanced"),
	Performance UMETA(DisplayName = "Performance"),
};

// General settings
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNGDeferDelete;

//------------------------------------------------------------------------------------------------------
// Console variables for NSS.
//------------------------------------------------------------------------------------------------------
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarEnableNSS;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarEnableNSSInEditor;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSDebug;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSDebugViewMode;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSAdjustMipBias;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSUseFragmentShader;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSShaderQualityMode;

// Console variables for NFRU.
//------------------------------------------------------------------------------------------------------
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarEnableNFRU;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUCaptureDebugUI;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUUpdateGlobalFrameTime;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUModifySlateDeltaTime;

#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || UE_BUILD_TEST)
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUOnlyInterpolatedFrames;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUShowDebugView;
#endif
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUPaceAdjuster;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUUpAdjustFrameCount;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUDownAdjustFrameCount;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNFRUUseFragmentShader;
//-------------------------------------------------------------------------------------
// Settings for Arm Neural Graphics exposed through the Editor UI.
//-------------------------------------------------------------------------------------
UCLASS(Config = Engine, DefaultConfig, DisplayName = "Neural Graphics")
class NGSETTINGS_API UNGSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	// General settings
	UPROPERTY(Config,
		EditAnywhere,
		Category = "NG",
		meta = (ConsoleVariable = "r.NG.DeferDelete",
			DisplayName = "Frames to defer deletion",
			EditCondition = "bNSSEnabled || bNFRUEnabled",
			ToolTip = "Defer deletion of resources that are too old to reuse"))
	int32 bNGDeferDelete;

	UPROPERTY(Config, EditAnywhere, Category = "NSS", meta = (ConsoleVariable = "r.NSS.Enable", DisplayName = "Enable"))
	bool bNSSEnabled;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.EnableInEditorViewport",
			DisplayName = "Enabled in Editor Viewport",
			EditCondition = "bNSSEnabled",
			ToolTip = "When enabled use NSS by default in the Editor viewports."))
	bool bNSSEnabledInEditorViewport;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.Debug",
			DisplayName = "Debug Mode",
			EditCondition = "bNSSEnabled",
			ToolTip = "Turn on NSS Debug mode"))
	bool bNSSDebug;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.AdjustMipBias",
			DisplayName = "Adjust Mip Bias",
			EditCondition = "bNSSEnabled",
			ToolTip = "NSS Adjust Mip Bias"))
	bool bNSSAdjustMipBias;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.ShaderQualityMode",
			DisplayName = "Shader Quality Mode",
			EditCondition = "bNSSEnabled",
			ToolTip = "NSS Shader Quality Mode\n0 = Quality\n1 = Balanced\n2 = Performance"))
	ENSSShaderQualityMode NSSShaderQualityMode = ENSSShaderQualityMode::Balanced;

	// NFRU
	UPROPERTY(
		Config, EditAnywhere, Category = "NFRU", meta = (ConsoleVariable = "r.NFRU.Enable", DisplayName = "Enable"))
	bool bNFRUEnabled;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NFRU",
		meta = (ConsoleVariable = "r.NFRU.CaptureDebugUI",
			DisplayName = "Capture Debug UI",
			EditCondition = "bNFRUEnabled",
			ToolTip =
				"Force Neural Frame Rate Upscaling to detect and copy any debug UI which only renders on the first invocation of Slate's DrawWindow command."))
	bool bCaptureDebugUI;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NFRU",
		meta = (ConsoleVariable = "r.NFRU.UpdateGlobalFrameTime",
			DisplayName = "Update Global Frame Time",
			EditCondition = "bNFRUEnabled",
			ToolTip =
				"Update the GAverageMS and GAverageFPS engine globals with the frame time & FPS including frame generation."))
	bool bUpdateGlobalFrameTime;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NFRU",
		meta = (ConsoleVariable = "r.NFRU.ModifySlateDeltaTime",
			DisplayName = "Modify Slate Delta Time",
			EditCondition = "bNFRUEnabled",
			ToolTip =
				"Set the FSlateApplication delta time to 0.0 when redrawing the UI for the 'Slate Redraw' UI mode to prevent widgets' NativeTick implementations updating incorrectly, ignored when using 'UI Extraction'."))
	bool bModifySlateDeltaTime;
};

class NGSettingsModule final : public IModuleInterface
{
public:
	// IModuleInterface implementation
	void StartupModule() override;
	void ShutdownModule() override;
};
