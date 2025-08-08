// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectMacros.h"
#include "NSS.generated.h"

class FNSSSceneViewExtension;
class UNNEModelData;

/// UObject to store settings for this plugin.
UCLASS(config = Engine, defaultconfig)
class NSS_API UNSSSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config, Category = Rendering, EditAnywhere)
	TSoftObjectPtr<UNNEModelData> NSSModelData = TSoftObjectPtr<UNNEModelData>(FSoftObjectPath(TEXT("/NSS/nss_v0_1_0_int8.nss_v0_1_0_int8")));

	UPROPERTY(config, Category = Rendering, EditAnywhere)
	FString NNERuntime = "NNERuntimeRDGMLExtensionsForVulkan";
};

class FNSSModule : public IModuleInterface
{
public:
	// IModuleInterface implementation.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnPostEngineInit();
	void OnAssetReimport(UObject* Obj);
	void OnObjectPropertyChanged(UObject* Obj, FPropertyChangedEvent& Event);

	FDelegateHandle OnPostEngineInitHandle;
	FDelegateHandle OnAssetReimportHandle;
	FDelegateHandle OnObjectPropertyChangedHandle;
	TSharedPtr<FNSSSceneViewExtension> SceneViewExtension;
};
