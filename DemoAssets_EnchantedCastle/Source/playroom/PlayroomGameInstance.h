// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PlayroomGameInstance.generated.h"

class FMyViewExtension;

/**
 * 
 */
UCLASS()
class PLAYROOM_API UPlayroomGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init() override;

	UFUNCTION(BlueprintPure)
	bool IsNssRunning();

private:
	TSharedPtr<FMyViewExtension, ESPMode::ThreadSafe> _viewExtension;
};
