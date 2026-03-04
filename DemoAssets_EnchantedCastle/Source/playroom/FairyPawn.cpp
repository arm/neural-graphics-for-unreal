// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "FairyPawn.h"
#include "FairyComponent.h"
#include "MyFloatingPawnMovement.h"
#include "GameFramework/CharacterMovementComponent.h"

AFairyPawn::AFairyPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMyFloatingPawnMovement>(Super::MovementComponentName))
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AFairyPawn::SetMoveMode(FFairyMoveMode mode)
{
	_moveMode = mode;
	_cooldown = 0.f;
}

void AFairyPawn::BeginPlay()
{
    Super::BeginPlay();

	_fairyComponent = FindComponentByClass<UFairyComponent>();
	_fairyComponent->OnLevelChange.AddUniqueDynamic(this, &AFairyPawn::OnFairyUpgrade);

	_movementComponent = FindComponentByClass<UMyFloatingPawnMovement>();

	int lv = _fairyComponent->Level();
	OnFairyUpgrade(lv);

	_floatingStart = GetRand(0.0f, 1.0f);
	_floatingSpeed = GetRand(0.8f, 1.0f);

	_floatingWanderDir = GetRand(0.0f, 1.0f) > 0.5f ? 1.0f : -1.0f;
	_floatingWanderLength = GetRand(0.5f, 3.0f) * lv * _floatingWanderDir;
}

void AFairyPawn::SetupRotateAround(const FVector2D& center, bool isClockwise /*= true*/)
{
	_rotateAroundCenter = center;
	_rotateAroundRadius = (FVector2D(GetActorLocation()) - center).Length();
	_isRotateAroundClockwise = isClockwise;
}

void AFairyPawn::OnFairyUpgrade(int level)
{
}

void AFairyPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	int lv = _fairyComponent->Level();

	_cooldown -= DeltaTime;
	if (_cooldown <= 0.f)
	{
		_cooldown = 0.1f;

		float speed = (100 - lv) * 0.01f * 0.5f + 0.5f; // 2x ~ 1x
		float scaleSpeed = 0.14f;
		float gameT = GetGameTimeSinceCreation();

		//UE_LOG(LogTemp, Display, TEXT("!!!!!!!!!: %.2lf"), playerDistance);

		if (_moveMode == FFairyMoveMode::Floating)
		{
			FVector dir = FVector::UpVector;
			dir *= FMath::Sin(speed * _floatingSpeed * (gameT + _floatingStart));
			//AddMovementInput(dir, scaleSpeed);
			_dir = dir;
			_scale = scaleSpeed;
		}
		else if (_moveMode == FFairyMoveMode::Wander)
		{
			FVector dir = FVector::ZeroVector;
			dir.X += FMath::Cos(speed * (gameT + _floatingStart)) * _floatingWanderLength;
			dir.Y += FMath::Sin(speed * (gameT + _floatingStart)) * _floatingWanderLength;
			//AddMovementInput(dir, scaleSpeed);
			_dir = dir;
			_scale = scaleSpeed;
		}
		else if (_moveMode == FFairyMoveMode::Track)
		{
			SetMoveMode(FFairyMoveMode::Wander);
			return;
		}
		else if (_moveMode == FFairyMoveMode::RotateAround)
		{
			if (_rotateAroundRadius > 0.f)
			{
				float moveDistance = _movementComponent->GetMaxSpeed() * DeltaTime;
				float moveDegree = moveDistance / (TWO_PI * _rotateAroundRadius) * (_isRotateAroundClockwise ? 1.f : -1.f);
				FVector currentPos = GetActorLocation();
				FVector currentCenter = FVector(_rotateAroundCenter, currentPos.Z);
				FVector currentFromCenterOffset = currentPos - currentCenter;
				FVector destFromCenterOffset = currentFromCenterOffset.RotateAngleAxis(moveDegree, FVector::ZAxisVector);
				_dir = (currentCenter + destFromCenterOffset) - currentPos;
				_dir.Normalize();
				_scale = (float)(_fairyComponent->LevelMax() - lv) / _fairyComponent->LevelMax() * 0.5f + 0.1f;
			}
			else
			{
				_scale = 0.f;
			}
		}
		else
		{
			_scale = 0.f;
		}
	}

	if (_scale > 0.f)
	{
		AddMovementInput(_dir, _scale);
	}
}