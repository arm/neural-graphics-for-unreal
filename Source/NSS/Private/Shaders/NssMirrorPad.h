// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT
#include "GlobalShader.h"
#include "SceneTextureParameters.h"
#include "ScreenPass.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"

class FNssMirrorPadPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FNssMirrorPadPS);
	SHADER_USE_PARAMETER_STRUCT(FNssMirrorPadPS, FGlobalShader);
	// clang-format off
	BEGIN_SHADER_PARAMETER_STRUCT (FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneColor)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneVelocity)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, InSceneDepth)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	// clang-format on

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{}
};
