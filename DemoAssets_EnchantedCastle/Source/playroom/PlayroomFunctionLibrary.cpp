// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#include "PlayroomFunctionLibrary.h"

bool UPlayroomFunctionLibrary::ParseCommandLineIntValue(const FString& name, int32& outValue)
{
    return FParse::Value(FCommandLine::Get(), *name, outValue);
}

bool UPlayroomFunctionLibrary::ParseCommandLineFloatValue(const FString& name, float& outValue)
{
    return FParse::Value(FCommandLine::Get(), *name, outValue);
}

bool UPlayroomFunctionLibrary::ParseCommandLineStringValue(const FString& name, FString& outValue)
{
    return FParse::Value(FCommandLine::Get(), *name, outValue);
}

bool UPlayroomFunctionLibrary::IsConsoleVariableExist(const FString& name)
{
    return IConsoleManager::Get().FindConsoleVariable(*name) != nullptr;
}
