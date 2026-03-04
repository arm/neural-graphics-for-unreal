// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT


#include "PlayroomGameInstance.h"

#include "SceneViewExtension.h"
#include "TemporalUpscaler.h"

class FMyViewExtension : public FSceneViewExtensionBase
{
public:
    FMyViewExtension(const FAutoRegister& AutoRegister)
        : FSceneViewExtensionBase(AutoRegister) {
    }

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override
    {
    }

    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
    {
    }

    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
    {
        FString original = currentTemporalUpscalerDebugName;

        const UE::Renderer::Private::ITemporalUpscaler* upscaler = InViewFamily.GetTemporalUpscalerInterface();
        currentTemporalUpscalerDebugName = upscaler != nullptr ? upscaler->GetDebugName() : TEXT("");

        if (currentTemporalUpscalerDebugName != original)
        {
            UE_LOG(LogTemp, Log, TEXT("3rd party temporal upscaler -> %s"), *currentTemporalUpscalerDebugName);
        }
    }

    FString currentTemporalUpscalerDebugName;
};

void UPlayroomGameInstance::Init()
{
	Super::Init();

    _viewExtension = FSceneViewExtensions::NewExtension<FMyViewExtension>();
}

bool UPlayroomGameInstance::IsNssRunning()
{
    return _viewExtension->currentTemporalUpscalerDebugName == "NSS";
}
