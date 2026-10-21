// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/DefaultPawn.h"
#include "FairyPawn.generated.h"

class UFairyComponent;
class UMyFloatingPawnMovement;
class UCharacterMovementComponent;

UENUM(BlueprintType)
enum class FFairyMoveMode : uint8
{
	None,
	Floating,
	Wander,
	Track,
	RotateAround
};

UCLASS()
class PLAYROOM_API AFairyPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	AFairyPawn(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(Blueprintcallable, Category = "Playroom")
	void SetMoveMode(FFairyMoveMode mode);

	UFUNCTION(Blueprintcallable, Category = "Playroom")
	void SetupRotateAround(const FVector2D& center, bool isClockwise = true);

	UFUNCTION(BlueprintImplementableEvent, Blueprintcallable, Category = "Playroom")
	float GetRand(float InMin, float InMax);


protected:
	UFUNCTION() void OnFairyUpgrade(int level);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UFairyComponent* _fairyComponent;
	UMyFloatingPawnMovement* _movementComponent;
	FFairyMoveMode _moveMode = FFairyMoveMode::Track;

	// Stay
	float _floatingStart = 1.0f;
	float _floatingSpeed = 1.0f;
	// Wander
	float _floatingWanderDir = 1.0f;
	float _floatingWanderLength = 1.0f;
	// Track
	float _trackingDistance = 50.0f;
	float _trackingLvBoostDistance = 2.0f;
	float _giveupDistance = 10.0f;
	// RotateAround
	FVector2D _rotateAroundCenter = FVector2D::ZeroVector;
	float _rotateAroundRadius = 0.0f;
	bool _isRotateAroundClockwise = true;

	FVector _dir;
	float _scale = 1.0f;
	float _cooldown = 0.0f;
};
