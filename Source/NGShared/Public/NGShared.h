// This file is part of the FidelityFX Super Resolution 3.1 Unreal Engine Plugin.
//
// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates open-source-office@arm.com
// SPDX-License-Identifier: MIT

#pragma once

#include "HAL/Platform.h"
#include "Misc/EngineVersionComparison.h"

// Variant of UE_VERSION_NEWER_THAN that is true if the engine version is at or later than the specified, used to better
// handle version differences in the codebase.
#define UE_VERSION_AT_LEAST(MajorVersion, MinorVersion, PatchVersion) \
	UE_GREATER_SORT(ENGINE_MAJOR_VERSION,                             \
		MajorVersion,                                                 \
		UE_GREATER_SORT(                                              \
			ENGINE_MINOR_VERSION, MinorVersion, UE_GREATER_SORT(ENGINE_PATCH_VERSION, PatchVersion, true)))

#if PLATFORM_WINDOWS
#define FFX_ENABLE_DX12 1
#include "Windows/AllowWindowsPlatformTypes.h"
#else
#define FFX_ENABLE_DX12 0
#define FFX_GCC
#endif
THIRD_PARTY_INCLUDES_START

#if UE_VERSION_AT_LEAST(5, 3, 0)
#include <bit>
#endif
#include "FidelityFX/host/ffx_types.h"
#include "ffx_api.h"
#include "ffx_api_types.h"

#if !defined(FFX_GCC)
#undef FFX_API
#define FFX_API __declspec(dllexport)
// #define FFXSHARED_API __declspec(dllexport)
#endif

#include "FidelityFX/gpu/frameinterpolation/ffx_frameinterpolation_resources.h"
#include "FidelityFX/host/ffx_assert.h"
#include "FidelityFX/host/ffx_error.h"
#include "FidelityFX/host/ffx_frameinterpolation.h"
#include "FidelityFX/host/ffx_interface.h"
#include "FidelityFX/host/ffx_util.h"
#include "ffx_framegeneration.h"

THIRD_PARTY_INCLUDES_END
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#else
#undef FFX_GCC
#endif
// this is repeated as #include "FidelityFX/host/ffx_util.h"
// #include <FidelityFX/arm_helpers.h>

// -----------------------------------------------------------------------------------------------------------------------------------------------
// These macros are used to create copies of UE5 objects that we can control via copy-paste from UE5 source, without
// modifying the UE5 source
//  after copying it.  Due to the single-pass nature of the C++ preprocessor, we can't automate this entire process with
//  macros.  The expected usage paradigm looks something like this:
//
//  1) Make all private members and methods inheritable by overriding the "private" keyword using a #define
//  2) Overwrite the name of the class by overriding that name with your own custom name using a #define
//  3) #include FFX_BUILD_VERSIONED_INCLUDE_PATH(<Name-of-Object>)
//  4) Clear the strings you overrode
//
// A concrete example:
//  #define private protected
//  #define FSlateApplication FFXFISlateApplicationAccessor
//  #include FFX_BUILD_VERSIONED_INCLUDE_PATH(SlateApplication)
//  #undef FSlateApplication
//  #undef private
// -----------------------------------------------------------------------------------------------------------------------------------------------
#pragma warning(disable : 5103)
// clang-format off
#define FFX_BUILD_INCLUDE_PATH_INNER(name, major, minor, patch) \
	F##name##Versions/F##name##_##major##_##minor##_##patch.h
// clang-format on
#define FFX_BUILD_INCLUDE_PATH(name, major, minor, patch) FFX_BUILD_INCLUDE_PATH_INNER(name, major, minor, patch)
#define FFX_STRINGIFY_INNER(x) #x
#define FFX_STRINGIFY(x) FFX_STRINGIFY_INNER(x)
#define FFX_BUILD_VERSIONED_INCLUDE_PATH(name) \
	FFX_STRINGIFY(FFX_BUILD_INCLUDE_PATH(name, ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, 0))