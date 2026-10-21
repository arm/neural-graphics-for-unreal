// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlayroomFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PLAYROOM_API UPlayroomFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	UFUNCTION(BlueprintPure)
	static bool ParseCommandLineIntValue(const FString& name, int32& outValue);
	UFUNCTION(BlueprintPure)
	static bool ParseCommandLineFloatValue(const FString& name, float& outValue);
	UFUNCTION(BlueprintPure)
	static bool ParseCommandLineStringValue(const FString& name, FString& outValue);
	UFUNCTION(BlueprintPure)
	static bool IsConsoleVariableExist(const FString& name);
};
