// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

DECLARE_DELEGATE_OneParam(FOnWorldReady, FString /* PLYDownloadURL */);
DECLARE_DELEGATE_OneParam(FOnWorldFailed, FString /* ErrorMessage */);
DECLARE_DELEGATE_TwoParams(FOnPollTick, FString /* OperationID */, FString /* Status */);
DECLARE_DELEGATE_OneParam(FOnSplatDownloaded, FString /* SavePath */);

#include "WorldLabsAPIClient.generated.h"

UCLASS(BlueprintType)
class VIRTUALPRODUCTIONSPLAT_API UWorldLabsAPIClient : public UObject
{
	GENERATED_BODY()

public:
	UWorldLabsAPIClient();

	virtual void BeginDestroy() override;

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString APIKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldLabs")
	FString WorldsBaseURL = TEXT("https://api.worldlabs.ai/marble/v1");

	FOnWorldReady OnWorldReady;
	FOnWorldFailed OnWorldFailed;
	FOnPollTick OnPollTick;
	FOnSplatDownloaded OnSplatDownloaded;

	UFUNCTION(BlueprintCallable, Category = "WorldLabs")
	void Init();

	UFUNCTION(BlueprintCallable, Category = "WorldLabs", meta = (AdvancedDisplay = "Model"))
	bool SubmitWorldGeneration(const FString& PanoramaPath, const FString& Prompt, const FString& Model);

	UFUNCTION(BlueprintCallable, Category = "WorldLabs")
	void PollJobStatus(const FString& OperationID);

	UFUNCTION(BlueprintCallable, Category = "WorldLabs")
	void DownloadPLYFile(const FString& URL, const FString& SavePath);

	void DownloadSPZFile(const FString& URL, const FString& SavePath);

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString CurrentOperationID;

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString CurrentStatus;

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString LastWorldId;

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString LastWorldUrl;

	UPROPERTY(BlueprintReadOnly, Category = "WorldLabs")
	FString CurrentWorldId;

private:
	void ApplyMarbleJsonHeaders(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request) const;

	FString GetMarbleBaseURL() const;
	FString GetAuthHeaderValue() const;

	void PostPrepareUpload(const FString& FullImagePath);
	void OnPrepareUploadResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void IssueGCSUpload(const FString& UploadUrl, const FString& FullFilePath, TSharedPtr<FJsonObject> RequiredHeadersJson);
	void OnGCSUploadResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void SubmitGenerationRequest(const FString& MediaAssetId);
	void OnGenerationResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void PollOperation(const FString& OperationID);
	void OnPollOperationResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void FetchWorldAsset(const FString& WorldID);
	void OnFetchWorldResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded);

	void OnDownloadResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FString SavePath);
	void OnDownloadSPZResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FString SavePath);

	FTimerHandle PollTimerHandle;
	float PollIntervalSeconds = 10.0f;
	FString PollingOperationID;

	FString PendingGenerationPrompt;
	FString PendingModelName;
	FString StagedMediaAssetId;
	FString StagedGCSUploadUrl;
	TSharedPtr<FJsonObject> StagedGCSRequiredHeaders;
	FString PendingLocalUploadPath;

	void StartPolling(const FString& OperationID);
	void StopPolling();

	static bool TryExtractPlyDownloadUrl(const TSharedPtr<FJsonObject>& Root, FString& OutUrl);
	static bool TryExtractPlyDownloadUrlFromValue(const TSharedPtr<FJsonValue>& Val, FString& OutUrl);

	static bool TryExtractSpzDownloadUrl(const TSharedPtr<FJsonObject>& Root, FString& OutUrl, const FString& DesiredKey);
};
