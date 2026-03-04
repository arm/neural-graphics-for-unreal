// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FairyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLevelChangeSignature, int, level);

UENUM(BlueprintType)
enum class FSHitResult : uint8
{
    Lose,
    Tie,
    Win
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PLAYROOM_API UFairyComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UFairyComponent();

    UPROPERTY(BlueprintAssignable)
    FLevelChangeSignature OnLevelChange;

    int Exp() { return _exp; }

    UFUNCTION(Blueprintcallable, BlueprintPure, Category = "Playroom") float GetLevelSize() { return 1.0f - (1.0f - LevelPercent()) * (1.0f - LevelPercent()); }
    UFUNCTION(Blueprintcallable, BlueprintPure, Category = "Playroom") float LevelPercent() { return _level * 1.0f / _levelMax; }
    UFUNCTION(Blueprintcallable, BlueprintPure, Category = "Playroom") int Level() { return _level; }
    UFUNCTION(Blueprintcallable, BlueprintPure, Category = "Playroom") int LevelMax() { return _levelMax; }
    UFUNCTION(Blueprintcallable, Category = "Playroom") void SetLevel(int level);
    UFUNCTION(Blueprintcallable, Category = "Playroom") void Upgrade();
    UFUNCTION(Blueprintcallable, Category = "Playroom") void Downgrade();
    UFUNCTION(Blueprintcallable, Category = "Playroom") FSHitResult OnHit(UFairyComponent* oppoent);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int minLevel = 1;

private:
    int _level = 1;    
    int _levelMax = 20;
    float _exp = 0.0f;
};