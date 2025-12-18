//
// Copyright © 2025 Arm Limited.
// SPDX-License-Identifier: MIT
//

#if WITH_EDITOR

#include "NGSettings.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Tests/AutomationCommon.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Application/SlateApplication.h"

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

// Load the test map, ensure each test load the same fixed map.
static bool TryLoadTestMap(const FString& MapName, FAutomationTestBase* Test)
{
	if (!AutomationOpenMap(MapName))
	{
		if (Test)
		{
			Test->AddError(FString::Printf(TEXT("Failed to open map %s"), *MapName));
		}
		return false;
	}
	return true;
}

static TAutoConsoleVariable<FString> CVarTestMap(
	TEXT("r.TestMap"),
	TEXT("/Game/ThirdPerson/Maps/ThirdPersonMap"),
	TEXT("The map to load for the Arm NSS tests. Ensure this map exists in your project.")
	);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmNSSEnableTest,
	"ArmNG.PluginTests.NSSEnableTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FArmNSSEnableTest::RunTest(const FString& Parameters)
{
	// 1. Enable the temporal upscaler visualizer.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

	// 2. Set the console variable to disable Arm NSS.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), false));

	// 3. Load a test map (ensure the map exists in your project)
	FString MapName = CVarTestMap.GetValueOnGameThread();
	if (!TryLoadTestMap(MapName, this))
	{
		return false;
	}

	// 4. Wait for the map to load and render. Use a latent command to delay execution.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

	// 5. Ensure the "Preview" or "PIE" window is present.
	TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
	TSharedPtr<SWindow> PreviewWindow;

	for (auto& Window : Windows)
	{
		FString Title = Window->GetTitle().ToString();
		if (Title.Contains(TEXT("Preview")) || Title.Contains(TEXT("PIE")))
		{
			PreviewWindow = Window;
			break;
		}
	}

	if (!PreviewWindow.IsValid())
	{
		return false;
	}

	// 6. Take a screenshot. This latent command will capture a screenshot and save it in your project's saved folder.
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("ArmNG_NSS_Enable_before.png"), PreviewWindow }));

	// 7. Enable NSS from the command line.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

	// 8. Wait before taking a screenshot.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("ArmNG_NSS_Enable_after.png"), PreviewWindow }));

	// 9. Set the NSS default: false.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmNSSDebugTest,
	"ArmNG.PluginTests.NSSDebugTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FArmNSSDebugTest::RunTest(const FString& Parameters)
{
	// 1. Enable the temporal upscaler visualizer.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

	// 2. Enable the NSS.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

	// 3. Load a test map (ensure the map exists in your project)
	FString MapName = CVarTestMap.GetValueOnGameThread();
	if (!TryLoadTestMap(MapName, this))
	{
		return false;
	}

	// 4. Wait for the map to load and render. Use a latent command to delay execution.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

	// 5. Disable the NSS debug mode.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Debug"), false));

	// 6. Ensure the "Preview" or "PIE" window is present.
	TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
	TSharedPtr<SWindow> PreviewWindow;
	for (auto& Window : Windows)
	{
		FString Title = Window->GetTitle().ToString();
		if (Title.Contains(TEXT("Preview")) || Title.Contains(TEXT("PIE")))
		{
			PreviewWindow = Window;
			break;
		}
	}

	if (!PreviewWindow.IsValid())
	{
		return false;
	}

	// 7. Wait before taking a screenshot.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("Arm_NSS_Debug_before.png"), PreviewWindow }));

	// 8. Enable the NSS debug mode.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Debug"), true));

	// 9. Wait before taking a screenshot.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("Arm_NSS_Debug_after.png"), PreviewWindow }));

	// 10. Set the NSS debug mode to default: false.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Debug"), false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmNSSAdjustMipBiasTest,
	"ArmNG.PluginTests.NSSAdjustMipBiasTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FArmNSSAdjustMipBiasTest::RunTest(const FString& Parameters)
{
	// 1. Enable the temporal upscaler visualizer so we can see when Arm ASR is running.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("ShowFlag.VisualizeTemporalUpscaler"), true));

	// 2. Enable the NSS.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.Enable"), true));

	// 3. Load a test map (ensure the map exists in your project)
	FString MapName = CVarTestMap.GetValueOnGameThread();
	if (!TryLoadTestMap(MapName, this))
	{
		return false;
	}

	// 4. Wait for the map to load and render. Use a latent command to delay execution.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.0f));

	// 5. Set the NSS AdjustMipBias to false.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.AdjustMipBias"), false));

	// 6. Ensure the "Preview" or "PIE" window is present.
	TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
	TSharedPtr<SWindow> PreviewWindow;

	for (auto& Window : Windows)
	{
		FString Title = Window->GetTitle().ToString();
		if (Title.Contains(TEXT("Preview")) || Title.Contains(TEXT("PIE")))
		{
			PreviewWindow = Window;
			break;
		}
	}

	if (!PreviewWindow.IsValid())
	{
		return false;
	}

	// 7. Wait before taking a screenshot.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("ArmNG_NSS_AdjustMipBias_False.png"), PreviewWindow }));

	// 8. Set the NSS AdjustMipBias to true.
	ADD_LATENT_AUTOMATION_COMMAND(FSetConsoleVariableLatentCommand(TEXT("r.NSS.AdjustMipBias"), true));

	// 9. Wait before taking a screenshot.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand({ TEXT("ArmNG_NSS_AdjustMipBias_True.png"), PreviewWindow }));

	return true;
}

#endif
