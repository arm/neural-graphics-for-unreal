// SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: MIT

#if WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Tests/AutomationCommon.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Application/SlateApplication.h"

#include "NSS.h"

// Class to enable setting console variables as latent commands.
class FSetConsoleVariableLatentCommand : public IAutomationLatentCommand
{
public:
    // Constructor: takes the name of the console variable and the float value to set.
    FSetConsoleVariableLatentCommand(const FString& InConsoleVarName, float InValue)
        : ConsoleVarName(InConsoleVarName)
        , Value(InValue)
        , bHasSet(false)
    {}

    // Update() is called every frame until it returns true.
    virtual bool Update() override
    {
        if (!bHasSet)
        {
            // Find the console variable by name.
            IConsoleVariable* ConsoleVar = IConsoleManager::Get().FindConsoleVariable(*ConsoleVarName);
            if (ConsoleVar)
            {
                // Set the console variable to the specified value.
                ConsoleVar->Set(Value, ECVF_SetByConsole);
                UE_LOG(LogTemp, Log, TEXT("Set console variable '%s' to %f."), *ConsoleVarName, Value);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Console variable '%s' not found."), *ConsoleVarName);
            }
            bHasSet = true;
        }
        // Return true immediately once the variable is set.
        return true;
    }

private:
    FString ConsoleVarName;
    float Value;
    bool bHasSet;
};

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FExecuteConsoleCommandLatentCommand, FString, Command);

bool FExecuteConsoleCommandLatentCommand::Update()
{
    if (GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->Exec(nullptr, *Command, *GLog);
        return true;
    }
    return false;
}

// Custom latent command to take a screenshot in game mode.
class FTakeScreenshotLatentCommand : public IAutomationLatentCommand
{
public:
    // Constructor: ScreenshotName is the base name (without extension) 
    // and InDelay is how long to wait after requesting the screenshot.
    FTakeScreenshotLatentCommand(const FString& InScreenshotName, float InDelay = 1.0f)
        : ScreenshotName(InScreenshotName)
        , Delay(InDelay)
        , bScreenshotRequested(false)
        , StartTime(0.0)
    {}

    virtual bool Update() override
    {
        if (!bScreenshotRequested)
        {
            // Record the start time when we request the screenshot.
            StartTime = FPlatformTime::Seconds();

            // Request the screenshot.
            // This call schedules the screenshot to be taken.
            FScreenshotRequest::RequestScreenshot(*(ScreenshotName + TEXT(".png")), false, false);
            bScreenshotRequested = true;

            UE_LOG(LogTemp, Log, TEXT("Screenshot requested: %s.png"), *ScreenshotName);
        }

        // Wait for the specified delay to allow the screenshot process to complete.
        double ElapsedTime = FPlatformTime::Seconds() - StartTime;
        return (ElapsedTime > Delay);
    }

private:
    FString ScreenshotName;
    float Delay;
    bool bScreenshotRequested;
    double StartTime;
};


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSSEnableTest,
    "NSS.PluginTests.EnablePluginTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext 
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
    | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNSSEnableTest::RunTest(const FString& Parameters)
{
    // 1. Enable the temporal upscaler visualizer so we can see when NSS is running.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

    // 2. Ensure NSS is disabled to start with.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.AntiAliasingMethod"), 2));
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), false));

    // 3. Load a test map (ensure the map exists in your project)
    const FString MapName = "/Game/_Game/ThirdPerson/ThirdPerson";
    if (!AutomationOpenMap(MapName))
    {
        AddError(FString::Printf(TEXT("Failed to open map %s"), *MapName));
        return false;
    }

    // 4. Wait for the map to load and render. Use a latent command to delay execution.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

    // 5. Take a screenshot. This latent command will capture a screenshot and save it in your project's saved folder.
    TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("NSS_EnablePluginTest_before.png"), CurrentWindow }));
    // 6. Enable NSS from the command line.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

    // 7. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("NSS_EnablePluginTest_after.png"), CurrentWindow }));

    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSSScreenPercentageTest,
    "NSS.PluginTests.UpscaleRatioTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
    | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

    bool FNSSScreenPercentageTest::RunTest(const FString& Parameters)
{
    // 1. Enable the temporal upscaler visualizer so we can see when NSS is running.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

    // 2. Ensure NSS is enabled.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.AntiAliasingMethod"), 2));
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

    // 3. Load a test map (ensure the map exists in your project)
    const FString MapName = "/Game/_Game/ThirdPerson/ThirdPerson";
    if (!AutomationOpenMap(MapName))
    {
        AddError(FString::Printf(TEXT("Failed to open map %s"), *MapName));
        return false;
    }

    // 4. Wait for the map to load and render. Use a latent command to delay execution.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

    // 5. Set screen percentage to 100.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.ScreenPercentage"), 100));

    // 6. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeActiveEditorScreenshotCommand(
        TEXT("NSS_ScreenPercentageTest_100.png")));

    // 5. Set screen percentage to 50.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.ScreenPercentage"), 50));

    // 8. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeActiveEditorScreenshotCommand(
        TEXT("NSS_ScreenPercentageTest_50.png")));

    // 7. Set screen percentage to 67.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.ScreenPercentage"), 67));

    // 8. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeActiveEditorScreenshotCommand(
        TEXT("NSS_ScreenPercentageTest_67.png")));

    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSSFilmGrainTest,
    "NSS.PluginTests.FilmGrainTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
    | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

    bool FNSSFilmGrainTest::RunTest(const FString &Parameters)
{
    // 1. Enable the temporal upscaler visualizer so we can see when NSS is running.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

    // 2. Ensure NSS is enabled.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.AntiAliasingMethod"), 2));
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

    // 3. Load a test map (ensure the map exists in your project)
    const FString MapName = "/Game/_Game/ThirdPerson/ThirdPerson";
    if (!AutomationOpenMap(MapName))
    {
        AddError(FString::Printf(TEXT("Failed to open map %s"), *MapName));
        return false;
    }

    // 4. Wait for the map to load and render. Use a latent command to delay execution.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

    // 5. Enable film grain (post process volume should exist in the test map with this enabled).
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.FilmGrain"), true));

    // 6. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeActiveEditorScreenshotCommand(TEXT("NSS_FilmGrain_on.png")));

    // 7. Disable film grain.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.FilmGrain"), false));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSSMobileEnableTest,
    "NSS.MobilePluginTests.EnablePluginTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
    | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

    bool FNSSMobileEnableTest::RunTest(const FString& Parameters)
{
    // 1. Ensure NSS is disabled to start with.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.Mobile.AntiAliasing"), 2));
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), false));

    // 2. Load a test map (ensure the map exists in your project)
    const FString MapName = "/Game/_Game/ThirdPerson/ThirdPerson";
    if (!AutomationOpenMap(MapName))
    {
        AddError(FString::Printf(TEXT("Failed to open map %s"), *MapName));
        return false;
    }

    // 3. Wait for the map to load and render. Use a latent command to delay execution.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

    // 4. Run command to visualise the upscaled output texture of NSS. This should do nothing as NSS is not running.
    ADD_LATENT_AUTOMATION_COMMAND(FExecuteConsoleCommandLatentCommand(TEXT("vis NSSUpsampledJitteredColour")));

    // 5. Take a screenshot. This latent command will capture a screenshot and save it in your project's saved folder.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("NSS_MobileEnablePluginTest_before.png"), CurrentWindow }));

    // 6. Enable NSS from the command line. Now the visualisation of the upscaled texture should appear.
    ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

    // 7. Wait before taking a screenshot.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("NSS_MobileEnablePluginTest_after.png"), CurrentWindow }));

    return true;
}

#endif