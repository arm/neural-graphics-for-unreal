// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

//------------------------------------------------------------------------------------------------------
// Console variables that control how NSS operates.
//------------------------------------------------------------------------------------------------------
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarEnableNSS;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarEnableNSSInEditor;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSDebug;
extern NGSETTINGS_API TAutoConsoleVariable<int32> CVarNSSAdjustMipBias;

//-------------------------------------------------------------------------------------
// Settings for Arm Neural Super Sampling 1.0 exposed through the Editor UI.
//-------------------------------------------------------------------------------------
UCLASS(Config = Engine, DefaultConfig, DisplayName = "Neural Super Sampling 1.0")
class NGSETTINGS_API UNGSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	UPROPERTY(Config, EditAnywhere, Category = "NSS", meta = (ConsoleVariable = "r.NSS.Enable", DisplayName = "Enable"))
	bool bNSSEnabled;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.EnableInEditorViewport",
			DisplayName = "Enabled in Editor Viewport",
			ToolTip = "When enabled use NSS by default in the Editor viewports."))
	bool bNSSEnabledInEditorViewport;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.Debug", DisplayName = "Debug Mode", ToolTip = "Turn on NSS Debug mode"))
	bool bNSSDebug;

	UPROPERTY(Config,
		EditAnywhere,
		Category = "NSS",
		meta = (ConsoleVariable = "r.NSS.AdjustMipBias",
			DisplayName = "Adjust Mip Bias",
			ToolTip = "NSS Adjust Mip Bias"))
	bool bNSSAdjustMipBias;
};

class NGSettingsModule final : public IModuleInterface
{
public:
	// IModuleInterface implementation
	void StartupModule() override;
	void ShutdownModule() override;
};
