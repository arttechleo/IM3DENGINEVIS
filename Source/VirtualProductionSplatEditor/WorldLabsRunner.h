// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldLabsRunner.generated.h"

class UWorldLabsAPIClient;
class SNotificationItem;

UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API AWorldLabsRunner : public AActor
{
	GENERATED_BODY()

public:
	AWorldLabsRunner();

	UPROPERTY(EditAnywhere, Category = "WorldLabs")
	FString WorldPrompt = TEXT("A photorealistic architectural interior matching the greybox layout");

	UPROPERTY(EditAnywhere, Category = "WorldLabs")
	FString ModelName = TEXT("Marble 0.1-mini");

	UPROPERTY(EditAnywhere, Category = "WorldLabs")
	FString GreyboxExportsDir;

	UPROPERTY(EditAnywhere, Category = "WorldLabs")
	FString PLYOutputDir;

	UPROPERTY(EditAnywhere, Category = "WorldLabs")
	bool bAutoImportOnComplete = true;

	UFUNCTION(CallInEditor, Category = "WorldLabs")
	void SubmitToWorldLabs();

	UFUNCTION(CallInEditor, Category = "WorldLabs")
	void CheckJobStatus();

	UFUNCTION(CallInEditor, Category = "WorldLabs")
	void DownloadSplat();

	UPROPERTY(VisibleAnywhere, Category = "WorldLabs")
	FString CurrentOperationID;

	UPROPERTY(VisibleAnywhere, Category = "WorldLabs")
	FString CurrentStatus;

	UPROPERTY(VisibleAnywhere, Category = "WorldLabs")
	FString CurrentDownloadURL;

	UPROPERTY(VisibleAnywhere, Category = "WorldLabs")
	FString DownloadedSplatPath;

	/** Set after world generation completes. Open at marble.worldlabs.ai/world/<id>. */
	UPROPERTY(VisibleAnywhere, Category = "WorldLabs")
	FString LastWorldPreviewURL;

	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UWorldLabsAPIClient> APIClient;

	void EnsureApiClient();

	void HandleSplatDownloaded(FString SavePath);
	void AutoImportSplat(const FString& DownloadedSPZPath);

	void HandleWorldReady(FString PLYDownloadURL);
	void HandleWorldFailed(FString ErrorMessage);
	void HandlePollTick(FString OperationID, FString Status);

	/** Post a new in-progress toast or update the existing one. Pass bSuccess/bFail to close it. */
	void PostOrUpdateNotification(const FString& Text, bool bSuccess = false, bool bFail = false);

	TWeakPtr<SNotificationItem> ActiveNotification;
	double PollStartSeconds = 0.0;
};
