// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("FrameGen"), STATGROUP_FrameGen, STATCAT_Advanced);

DECLARE_FLOAT_COUNTER_STAT(
	TEXT("Render FPS: FPS of real rendered frames."), STAT_FrameGen_RenderFPS, STATGROUP_FrameGen);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Render Interval: Time interval(ms) between two real rendered frames."),
	STAT_FrameGen_RenderInterval,
	STATGROUP_FrameGen);

DECLARE_FLOAT_COUNTER_STAT(
	TEXT("Present FPS: FPS of frames presented to the display."), STAT_FrameGen_PresentFPS, STATGROUP_FrameGen);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Present Interval: Time interval(ms) between two frames to present."),
	STAT_FrameGen_PresentInterval,
	STATGROUP_FrameGen);