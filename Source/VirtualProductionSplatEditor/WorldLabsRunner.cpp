// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldLabsRunner.h"
#include "WorldLabsAPIClient.h"
#include "WorldLabsPromptHistoryStore.h"
#include "MultiAngleCameraRig.h"
#include "GaussianSplatImportRunner.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

AWorldLabsRunner::AWorldLabsRunner()
{
	PrimaryActorTick.bCanEverTick = false;
	GreyboxExportsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GreyboxExports"));
	PLYOutputDir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("GaussianSplats"));
}

void AWorldLabsRunner::PostOrUpdateNotification(const FString& Text, bool bSuccess, bool bFail)
{
	if (TSharedPtr<SNotificationItem> Item = ActiveNotification.Pin())
	{
		Item->SetText(FText::FromString(Text));
		if (bSuccess)
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
			Item->ExpireAndFadeout();
			ActiveNotification.Reset();
		}
		else if (bFail)
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
			Item->ExpireAndFadeout();
			ActiveNotification.Reset();
		}
	}
	else
	{
		FNotificationInfo Info(FText::FromString(Text));
		if (!bSuccess && !bFail)
		{
			Info.bFireAndForget = false;
			Info.bUseThrobber = true;
			Info.bUseSuccessFailIcons = true;
		}
		else
		{
			Info.ExpireDuration = 4.0f;
			Info.bUseLargeFont = false;
		}
		TSharedPtr<SNotificationItem> NewItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (!bSuccess && !bFail)
		{
			ActiveNotification = NewItem;
		}
	}
}

void AWorldLabsRunner::BeginPlay()
{
	Super::BeginPlay();
	EnsureApiClient();
}

void AWorldLabsRunner::EnsureApiClient()
{
	if (APIClient)
	{
		return;
	}
	APIClient = NewObject<UWorldLabsAPIClient>(this);
	APIClient->Init();
	APIClient->OnWorldReady.BindUObject(this, &AWorldLabsRunner::HandleWorldReady);
	APIClient->OnWorldFailed.BindUObject(this, &AWorldLabsRunner::HandleWorldFailed);
	APIClient->OnPollTick.BindUObject(this, &AWorldLabsRunner::HandlePollTick);
	APIClient->OnSplatDownloaded.BindUObject(this, &AWorldLabsRunner::HandleSplatDownloaded);
}

void AWorldLabsRunner::SubmitToWorldLabs()
{
	EnsureApiClient();
	if (!APIClient)
	{
		return;
	}

	const FString Dir = FPaths::ConvertRelativePathToFull(GreyboxExportsDir);
	FString PanoramaPath;

	// Prefer stitched panorama at GreyboxExports root (not faces/).
	const FString DefaultPano = FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, TEXT("panorama_360.png")));
	if (FPaths::FileExists(DefaultPano))
	{
		PanoramaPath = DefaultPano;
	}

	if (PanoramaPath.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APanoramicCapture360> It(World); It; ++It)
			{
				const APanoramicCapture360* Pano = *It;
				if (Pano && !Pano->LastSavedPath.IsEmpty())
				{
					const FString Full = FPaths::ConvertRelativePathToFull(Pano->LastSavedPath);
					// Ignore directories (e.g. faces/) — only use a concrete PNG path.
					if (FPaths::FileExists(Full) && !FPaths::DirectoryExists(Full))
					{
						PanoramaPath = Full;
						break;
					}
				}
			}
		}
	}

	if (PanoramaPath.IsEmpty())
	{
		TArray<FString> PNGFiles;
		IFileManager::Get().IterateDirectory(
			*Dir,
			[&PNGFiles](const TCHAR* Path, bool bIsDirectory) -> bool
			{
				if (bIsDirectory)
				{
					return true;
				}
				const FString S(Path);
				if (!S.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
				{
					return true;
				}
				if (S.Contains(TEXT("/faces/"), ESearchCase::IgnoreCase) || S.Contains(TEXT("\\faces\\"), ESearchCase::IgnoreCase))
				{
					return true;
				}
				PNGFiles.Add(FPaths::ConvertRelativePathToFull(S));
				return true;
			});
		PNGFiles.Sort();
		for (const FString& P : PNGFiles)
		{
			if (P.Contains(TEXT("panorama"), ESearchCase::IgnoreCase))
			{
				PanoramaPath = P;
				break;
			}
		}
		if (PanoramaPath.IsEmpty() && PNGFiles.Num() > 0)
		{
			PanoramaPath = PNGFiles[0];
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SubmitToWorldLabs: panorama path = %s (GreyboxExportsDir=%s)"), *PanoramaPath, *GreyboxExportsDir);

	if (PanoramaPath.IsEmpty() || !FPaths::FileExists(PanoramaPath))
	{
		UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: no panorama PNG — run StitchPanorama after CaptureFaces (expected %s)."), *DefaultPano);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: submitting panorama to WorldLabs."));
	PollStartSeconds = FPlatformTime::Seconds();
	PostOrUpdateNotification(TEXT("WorldLabs: uploading panorama…"));
	APIClient->SubmitWorldGeneration(PanoramaPath, WorldPrompt, ModelName);
}

void AWorldLabsRunner::CheckJobStatus()
{
	EnsureApiClient();
	if (APIClient)
	{
		const FString Id = CurrentOperationID.IsEmpty() ? APIClient->CurrentOperationID : CurrentOperationID;
		APIClient->PollJobStatus(Id);
		CurrentOperationID = APIClient->CurrentOperationID;
		CurrentStatus = APIClient->CurrentStatus;
	}
}

void AWorldLabsRunner::DownloadSplat()
{
	EnsureApiClient();
	if (!APIClient || CurrentDownloadURL.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: CurrentDownloadURL is empty — wait for job to complete."));
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::ConvertRelativePathToFull(PLYOutputDir), true);
	const FString FileName = CurrentOperationID.IsEmpty()
		? TEXT("WorldLabs_export.ply")
		: FString::Printf(TEXT("WorldLabs_%s.ply"), *CurrentOperationID);
	const FString Dest = FPaths::Combine(FPaths::ConvertRelativePathToFull(PLYOutputDir), FileName);
	APIClient->DownloadPLYFile(CurrentDownloadURL, Dest);
}

void AWorldLabsRunner::HandleWorldReady(FString PLYDownloadURL)
{
	CurrentDownloadURL = PLYDownloadURL;
	CurrentOperationID = APIClient ? APIClient->CurrentOperationID : CurrentOperationID;
	CurrentStatus = APIClient ? APIClient->CurrentStatus : TEXT("complete");
	if (APIClient)
	{
		FWorldLabsPromptHistoryStore::UpdateLatestWorldFields(APIClient->LastWorldId, APIClient->LastWorldUrl);
		if (!APIClient->LastWorldId.IsEmpty())
		{
			LastWorldPreviewURL = FString::Printf(TEXT("https://marble.worldlabs.ai/world/%s"), *APIClient->LastWorldId);
		}
	}
	PostOrUpdateNotification(TEXT("WorldLabs: download started…"));
	UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: splat URL resolved. PreviewURL=%s"), *LastWorldPreviewURL);
}

void AWorldLabsRunner::HandleWorldFailed(FString ErrorMessage)
{
	UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: %s"), *ErrorMessage);
	PostOrUpdateNotification(FString::Printf(TEXT("WorldLabs: failed — %s"), *ErrorMessage), false, true);
}

void AWorldLabsRunner::HandlePollTick(FString OperationID, FString Status)
{
	CurrentOperationID = OperationID;
	CurrentStatus = Status;
	const int32 Elapsed = FMath::FloorToInt(static_cast<float>(FPlatformTime::Seconds() - PollStartSeconds));
	PostOrUpdateNotification(FString::Printf(TEXT("WorldLabs: generating world… (%ds elapsed)"), Elapsed));
	UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: poll — operation %s status %s"), *OperationID, *Status);
}

void AWorldLabsRunner::HandleSplatDownloaded(FString SavePath)
{
	DownloadedSplatPath = SavePath;
	UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: SPZ downloaded to %s"), *DownloadedSplatPath);
	PostOrUpdateNotification(TEXT("WorldLabs: converting SPZ → PLY…"));

	if (bAutoImportOnComplete)
	{
		AutoImportSplat(DownloadedSplatPath);
	}
}


void AWorldLabsRunner::AutoImportSplat(const FString& DownloadedSPZPath)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: AutoImportSplat: no world."));
		PostOrUpdateNotification(TEXT("WorldLabs: import failed — no world"), false, true);
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
		UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: AutoImportSplat: spawning AGaussianSplatImportRunner."));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Runner = World->SpawnActor<AGaussianSplatImportRunner>(
			AGaussianSplatImportRunner::StaticClass(), FTransform::Identity, SpawnParams);
		if (!Runner)
		{
			UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: AutoImportSplat: failed to spawn AGaussianSplatImportRunner."));
			PostOrUpdateNotification(TEXT("WorldLabs: import failed — could not spawn runner"), false, true);
			return;
		}
	}

	Runner->PLYFilePath = DownloadedSPZPath;
	Runner->ImportPLYIntoLevel();

	if (Runner->LastSpawnedSplat)
	{
		UE_LOG(LogTemp, Log, TEXT("WorldLabsRunner: AutoImportSplat succeeded: %s"), *Runner->LastSpawnedSplat->GetName());
		PostOrUpdateNotification(TEXT("WorldLabs: splat imported into level"), true, false);
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("WorldLabsRunner: AutoImportSplat failed for %s"), *DownloadedSPZPath);
	PostOrUpdateNotification(TEXT("WorldLabs: import failed — check log"), false, true);
}
