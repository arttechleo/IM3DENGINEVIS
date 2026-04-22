// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPToolbarExtension.h"
#include "GreyboxSceneBuilder.h"
#include "WorldLabsPromptDialog.h"
#include "WorldLabsRunner.h"
#include "Editor.h"
#include "IPythonScriptPlugin.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditor.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "PythonScriptTypes.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/SWindow.h"
#include "GaussianSplatImportRunner.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "VPToolbarExtension"

FDelegateHandle FVPToolbarExtension::MenuStartupHandle;

static FString NormalizePythonPath(const FString& Path)
{
	FString P = FPaths::ConvertRelativePathToFull(Path);
	P.ReplaceInline(TEXT("\\"), TEXT("/"));
	return P;
}

static bool RunPythonFile(const FString& AbsolutePath)
{
	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	if (!Py)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: PythonScriptPlugin not loaded."));
		return false;
	}
	if (!Py->IsPythonInitialized())
	{
		Py->ForceEnablePythonAtRuntime();
	}
	if (!Py->IsPythonAvailable() || !Py->IsPythonInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: Python not available."));
		return false;
	}

	const FString Cmd = FString::Printf(
		TEXT("exec(open(r'%s', encoding='utf-8').read())"),
		*NormalizePythonPath(AbsolutePath));

	FPythonCommandEx PyCmd;
	PyCmd.Command = Cmd;
	PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
	const bool bOk = Py->ExecPythonCommandEx(PyCmd);
	if (!bOk)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: Python failed: %s"), *PyCmd.CommandResult);
	}
	return bOk;
}

void FVPToolbarExtension::OnSetupVPLevel()
{
	const FString SpawnScript = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/VirtualProductionSplatEditor/SpawnPipelineActors.py"));
	if (!FPaths::FileExists(SpawnScript))
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: missing %s"), *SpawnScript);
		return;
	}
	if (!RunPythonFile(SpawnScript))
	{
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no editor world."));
		return;
	}

	TArray<AActor*> Builders;
	UGameplayStatics::GetAllActorsOfClass(World, AGreyboxSceneBuilder::StaticClass(), Builders);
	if (Builders.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no AGreyboxSceneBuilder in level after spawn script."));
		return;
	}

	if (AGreyboxSceneBuilder* Builder = Cast<AGreyboxSceneBuilder>(Builders[0]))
	{
		Builder->BuildGreyboxScene();
	}

	UE_LOG(LogTemp, Warning, TEXT("VP Level setup complete. Run Tools → VP Pipeline → Capture 360 Panorama (or SetupPanoramicRig.py)."));
}

void FVPToolbarExtension::OnCapturePanorama()
{
	const FString Script = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/VirtualProductionSplatEditor/SetupPanoramicRig.py"));
	if (!FPaths::FileExists(Script))
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: missing %s"), *Script);
		return;
	}
	RunPythonFile(Script);
}

void FVPToolbarExtension::OnSubmitToWorldLabs()
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no editor world."));
		return;
	}

	TArray<AActor*> Runners;
	UGameplayStatics::GetAllActorsOfClass(World, AWorldLabsRunner::StaticClass(), Runners);
	if (Runners.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no AWorldLabsRunner in level."));
		return;
	}

	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(NSLOCTEXT("VPToolbarExtension", "SubmitWLTitle", "Submit to WorldLabs"))
		.ClientSize(FVector2D(600.f, 500.f))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::UserSized);

	TSharedRef<SWorldLabsPromptDialog> DialogContent = SNew(SWorldLabsPromptDialog).ParentWindow(DialogWindow);
	DialogWindow->SetContent(DialogContent);

	GEditor->EditorAddModalWindow(DialogWindow);

	if (!DialogContent->WasSubmitted())
	{
		return;
	}

	const FString Prompt = DialogContent->GetPrompt();
	const FString Model = DialogContent->GetModel();

	if (AWorldLabsRunner* Runner = Cast<AWorldLabsRunner>(Runners[0]))
	{
		Runner->WorldPrompt = Prompt;
		Runner->ModelName = Model;
		Runner->SubmitToWorldLabs();
	}
}

void FVPToolbarExtension::OnImportLatestSplat()
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no editor world."));
		return;
	}

	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("GaussianSplats")));
	if (!FPaths::DirectoryExists(Dir))
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: GaussianSplats directory not found: %s"), *Dir);
		return;
	}

	FString LatestSpzPath;
	FDateTime LatestTime(0);

	IFileManager::Get().IterateDirectory(
		*Dir,
		[&LatestSpzPath, &LatestTime](const TCHAR* Path, bool bIsDirectory) -> bool
		{
			if (bIsDirectory)
			{
				return true;
			}
			const FString S(Path);
			if (!S.EndsWith(TEXT(".spz"), ESearchCase::IgnoreCase))
			{
				return true;
			}

			const FDateTime TS = IFileManager::Get().GetTimeStamp(Path);
			if (LatestSpzPath.IsEmpty() || TS > LatestTime)
			{
				LatestTime = TS;
				LatestSpzPath = S;
			}
			return true;
		});

	if (LatestSpzPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no .spz found in %s"), *Dir);
		return;
	}

	AGaussianSplatImportRunner* Runner = nullptr;
	for (TActorIterator<AGaussianSplatImportRunner> It(World); It; ++It)
	{
		Runner = *It;
		break;
	}
	if (!Runner)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: AGaussianSplatImportRunner not found in level."));
		return;
	}

	Runner->PLYFilePath = LatestSpzPath;
	Runner->ImportPLYIntoLevel();

	const bool bOk = Runner->LastSpawnedSplat != nullptr;
	const FString Msg = bOk ? FString::Printf(TEXT("Imported splat: %s"), *LatestSpzPath) : FString::Printf(TEXT("Import failed: %s"), *LatestSpzPath);

	FNotificationInfo Info(FText::FromString(Msg));
	Info.ExpireDuration = 4.0f;
	Info.bUseLargeFont = false;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void FVPToolbarExtension::OnOpenWorldPreview()
{
	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("VPToolbar: no editor world."));
		return;
	}

	AWorldLabsRunner* Runner = nullptr;
	for (TActorIterator<AWorldLabsRunner> It(World); It; ++It)
	{
		Runner = *It;
		break;
	}

	if (!Runner || Runner->LastWorldPreviewURL.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("VPToolbar: no preview URL yet — submit to WorldLabs first."));

		FNotificationInfo Info(FText::FromString(TEXT("No preview URL yet — submit to WorldLabs first.")));
		Info.ExpireDuration = 3.0f;
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FPlatformProcess::LaunchURL(*Runner->LastWorldPreviewURL, nullptr, nullptr);
}

void FVPToolbarExtension::Register()
{
	MenuStartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("VirtualProductionSplatEditor"));
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection(
			"VPPipeline",
			NSLOCTEXT("VPToolbarExtension", "VPPipelineSection", "VP Pipeline"));

		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"VPPipeline_SetupLevel",
			NSLOCTEXT("VPToolbarExtension", "SetupVPLevel", "Setup VP Level"),
			NSLOCTEXT("VPToolbarExtension", "SetupVPLevelTooltip", "Spawn pipeline actors (Python) and build greybox scene with sky."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tools"),
			FUIAction(FExecuteAction::CreateStatic(&FVPToolbarExtension::OnSetupVPLevel))));

		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"VPPipeline_Capture360",
			NSLOCTEXT("VPToolbarExtension", "Capture360", "Capture 360 Panorama"),
			NSLOCTEXT("VPToolbarExtension", "Capture360Tooltip", "Run SetupPanoramicRig.py (faces + stitch)."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tools"),
			FUIAction(FExecuteAction::CreateStatic(&FVPToolbarExtension::OnCapturePanorama))));

		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"VPPipeline_SubmitWorldLabs",
			NSLOCTEXT("VPToolbarExtension", "SubmitWorldLabs", "Submit to WorldLabs"),
			NSLOCTEXT("VPToolbarExtension", "SubmitWorldLabsTooltip", "Submit panorama_360.png to WorldLabs API."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tools"),
			FUIAction(FExecuteAction::CreateStatic(&FVPToolbarExtension::OnSubmitToWorldLabs))));

		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"VPPipeline_ImportLatestSplat",
			NSLOCTEXT("VPToolbarExtension", "ImportSplat", "Import Splat"),
			NSLOCTEXT("VPToolbarExtension", "ImportSplatTooltip", "Import newest .spz from Content/GaussianSplats."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tools"),
			FUIAction(FExecuteAction::CreateStatic(&FVPToolbarExtension::OnImportLatestSplat))));

		Section.AddEntry(FToolMenuEntry::InitMenuEntry(
			"VPPipeline_OpenWorldPreview",
			NSLOCTEXT("VPToolbarExtension", "OpenWorldPreview", "Open World Preview"),
			NSLOCTEXT("VPToolbarExtension", "OpenWorldPreviewTooltip", "Open the generated world in the WorldLabs browser preview (available after job completes)."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.OpenLevel"),
			FUIAction(FExecuteAction::CreateStatic(&FVPToolbarExtension::OnOpenWorldPreview))));
	}));
}

void FVPToolbarExtension::Unregister()
{
	if (MenuStartupHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(MenuStartupHandle);
		MenuStartupHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
