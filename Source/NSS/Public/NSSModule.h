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

#include "Modules/ModuleManager.h"
#include "NGShared.h"
#include "PostProcess/TemporalAA.h"
#include "RHIDefinitions.h"

class NSS;
class NSSViewExtension;

using INSS = ITemporalUpscaler;

//-------------------------------------------------------------------------------------
// In order for the NSS plugin to support the movie render pipeline some functions have to be exposed.
// This allows the separate NSSMovieRenderPipeline to behave consistently with the main NSS plugin.
//-------------------------------------------------------------------------------------
class INSSModule : public IModuleInterface
{
public:
	virtual NSS* GetNSSUpscaler() const = 0;
	virtual INSS* GetTemporalUpscaler() const = 0;
	virtual bool IsPlatformSupported(EShaderPlatform Platform) const = 0;
	virtual void SetEnabledInEditor(bool bEnabled) = 0;
};

class NSSModule final : public INSSModule
{
public:
	// IModuleInterface implementation
	void StartupModule() override;
	void ShutdownModule() override;

	static bool IsInitialized();

	void SetTemporalUpscaler(TSharedPtr<NSS, ESPMode::ThreadSafe> Upscaler);

	void OnPostEngineInit();

	NSS* GetNSSUpscaler() const;
	INSS* GetTemporalUpscaler() const;
	bool IsPlatformSupported(EShaderPlatform Platform) const;
	void SetEnabledInEditor(bool bEnabled);

private:
	TSharedPtr<NSS, ESPMode::ThreadSafe> TemporalUpscaler;
	TSharedPtr<NSSViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
