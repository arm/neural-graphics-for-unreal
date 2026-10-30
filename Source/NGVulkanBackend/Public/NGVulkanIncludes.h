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

// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "HAL/Platform.h"
#include "Misc/EngineVersionComparison.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/VulkanWindowsPlatform.h"
#pragma warning(push)
#pragma warning(disable : 4191)
#else
#define _countof(a) (sizeof(a) / sizeof(*(a)))
#define strcpy_s(a, b) strcpy(a, b)
#define FFX_GCC 1
#endif

THIRD_PARTY_INCLUDES_START

#include "FidelityFX/host/ffx_types.h"
#include <bit>

#if !defined(FFX_GCC)
#undef FFX_API
#define FFX_API __declspec(dllexport)
#endif

#include "ffx_api/ffx_api_loader.h"
#include "ffx_api/vk/ffx_api_vk.h"

THIRD_PARTY_INCLUDES_END
#if PLATFORM_WINDOWS
#pragma warning(pop)
#include "Windows/HideWindowsPlatformTypes.h"
#else
#undef _countof
#undef strcpy_s
#undef FFX_GCC
#endif
