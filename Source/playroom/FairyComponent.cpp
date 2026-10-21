// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "FairyComponent.h"

// Sets default values for this component's properties
UFairyComponent::UFairyComponent()
{
}

FSHitResult UFairyComponent::OnHit(UFairyComponent* oppoent)
{
    if (_level > oppoent->Level())
    {
        _exp += 1 / powf(_level - oppoent->Level(), 0.5f);
        if (_exp > 1.0f)
        {
            _exp = 0.0f;
            Upgrade();
        }

        return FSHitResult::Win;
    }
    else if (_level < oppoent->Level())
    {
        return FSHitResult::Lose;
    }

    return FSHitResult::Tie;
}

void UFairyComponent::SetLevel(int level)
{
    if (level > _levelMax || level < minLevel)
        return;

    _level = level;
    OnLevelChange.Broadcast(level);
}

void UFairyComponent::Upgrade()
{
    SetLevel(_level + 1);
}

void UFairyComponent::Downgrade()
{
    SetLevel(_level - 1);
}
