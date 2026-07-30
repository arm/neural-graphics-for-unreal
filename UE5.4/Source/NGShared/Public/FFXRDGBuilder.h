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

#pragma once

#include "FFXRDGBuilderBase.h"

class FFXRDGBuilder : public FFXRDGBuilderBase
{
public:
	FRDGTextureRef FindTexture(TCHAR const* Name)
	{
		for (FRDGTextureHandle It = Textures.Begin(); It != Textures.End(); ++It)
		{
			FRDGTextureRef Texture = Textures.Get(It);
			if (FCString::Strcmp(Texture->Name, Name) == 0)
			{
				return Texture;
			}
		}
		return nullptr;
	}
};

static_assert(sizeof(FRDGBuilder) == sizeof(FFXRDGBuilder), "FFXRDGBuilder must match the layout of FRDGBuilder so we can access the Lumen reflection texture!");