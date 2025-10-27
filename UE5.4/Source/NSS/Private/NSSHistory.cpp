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

#include "NSSHistory.h"

#include "NSS.h"
#include "NSSModule.h"

const TCHAR* NSSHistory::FfxNssDebugName = TEXT("NSS");

TCHAR const* NSSHistory::GetUpscalerName()
{
	return FfxNssDebugName;
}

NSSHistory::NSSHistory(NSSStateRef NewState, NSS* _Upscaler)
{
	Upscaler = _Upscaler;
	SetState(NewState);
}

NSSHistory::~NSSHistory()
{
	if (NSSModule::IsInitialized() && Upscaler)
	{
		Upscaler->ReleaseState(Nss);
	}
}

const TCHAR* NSSHistory::GetDebugName() const
{
	// this has to match NSSHistory::GetDebugName()
	return FfxNssDebugName;
}

uint64 NSSHistory::GetGPUSizeBytes() const
{
	// 5.3 not done
	return 0;
}

void NSSHistory::SetState(NSSStateRef NewState)
{
	if (Upscaler)
	{
		Upscaler->ReleaseState(Nss);
	}
	Nss = NewState;
}

ffxContext* NSSHistory::GetNSSContext() const
{
	return Nss.IsValid() ? &Nss->Nss : nullptr;
}

ffxApiCreateContextDescNss* NSSHistory::GetNSSContextDesc() const
{
	return Nss.IsValid() ? &Nss->Params : nullptr;
}
