// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldLabsAPIClient.h"
#include "VirtualProductionSplat.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

UWorldLabsAPIClient::UWorldLabsAPIClient() = default;

void UWorldLabsAPIClient::BeginDestroy()
{
	StopPolling();
	Super::BeginDestroy();
}

FString UWorldLabsAPIClient::GetMarbleBaseURL() const
{
	FString Base = WorldsBaseURL;
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}
	if (Base.IsEmpty())
	{
		Base = TEXT("https://api.worldlabs.ai/marble/v1");
	}
	return Base;
}

FString UWorldLabsAPIClient::GetAuthHeaderValue() const
{
	return APIKey;
}

void UWorldLabsAPIClient::ApplyMarbleJsonHeaders(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request) const
{
	Request->SetHeader(TEXT("WLT-Api-Key"), GetAuthHeaderValue());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
}

void UWorldLabsAPIClient::Init()
{
	if (!GConfig)
	{
		UE_LOG(LogVPSplat, Warning, TEXT("WorldLabsAPIClient::Init: GConfig is null."));
		return;
	}
	GConfig->GetString(TEXT("WorldLabsAPI"), TEXT("APIKey"), APIKey, GGameIni);

	{
		FString AnthropicAPIKey;
		GConfig->GetString(TEXT("AnthropicAPI"), TEXT("APIKey"), AnthropicAPIKey, GGameIni);
		UE_LOG(LogVPSplat, Warning, TEXT("WorldLabsAPIClient::Init: Anthropic key length = %d"), AnthropicAPIKey.Len());
	}

	WorldsBaseURL = TEXT("https://api.worldlabs.ai/marble/v1");
	FString ConfigBase;
	GConfig->GetString(TEXT("WorldLabsAPI"), TEXT("WorldsBaseURL"), ConfigBase, GGameIni);
	if (ConfigBase.StartsWith(TEXT("https://")))
	{
		WorldsBaseURL = ConfigBase;
	}

	UE_LOG(LogVPSplat, Warning, TEXT("Init: GGameIni path = %s"), *GGameIni);
	UE_LOG(LogVPSplat, Warning, TEXT("Init: APIKey length = %d"), APIKey.Len());
	UE_LOG(LogVPSplat, Warning, TEXT("Init: WorldsBaseURL resolved = %s"), *GetMarbleBaseURL());
	UE_LOG(LogVPSplat, Log, TEXT("WorldLabsAPIClient::Init: Marble base=%s (API key %s)"),
		*GetMarbleBaseURL(), APIKey.IsEmpty() ? TEXT("MISSING") : TEXT("set"));
}

bool UWorldLabsAPIClient::SubmitWorldGeneration(const FString& PanoramaPath, const FString& Prompt, const FString& Model)
{
	Init();
	UE_LOG(LogVPSplat, Warning, TEXT("SubmitWorldGeneration: APIKey length = %d, panorama = %s"), APIKey.Len(), *PanoramaPath);

	if (APIKey.IsEmpty())
	{
		UE_LOG(LogVPSplat, Error, TEXT("SubmitWorldGeneration: APIKey is empty — set [WorldLabsAPI] APIKey in DefaultGame.ini."));
		OnWorldFailed.ExecuteIfBound(TEXT("API key missing"));
		return false;
	}
	if (PanoramaPath.IsEmpty())
	{
		UE_LOG(LogVPSplat, Error, TEXT("SubmitWorldGeneration: empty panorama path."));
		OnWorldFailed.ExecuteIfBound(TEXT("No panorama path"));
		return false;
	}

	const FString Full = FPaths::ConvertRelativePathToFull(PanoramaPath);
	if (!FPaths::FileExists(Full))
	{
		UE_LOG(LogVPSplat, Error, TEXT("SubmitWorldGeneration: file not found: %s"), *Full);
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("Missing file: %s"), *Full));
		return false;
	}

	StopPolling();
	CurrentOperationID.Reset();
	CurrentStatus = TEXT("uploading");
	LastWorldId.Reset();
	LastWorldUrl.Reset();
	PendingGenerationPrompt = Prompt;
	PendingModelName = Model.IsEmpty() ? FString(TEXT("Marble 0.1-mini")) : Model;
	StagedMediaAssetId.Reset();
	StagedGCSUploadUrl.Reset();
	StagedGCSRequiredHeaders.Reset();

	PostPrepareUpload(Full);
	return true;
}

void UWorldLabsAPIClient::PostPrepareUpload(const FString& FullImagePath)
{
	PendingLocalUploadPath = FullImagePath;
	const FString FileName = FPaths::GetCleanFilename(FullImagePath);
	const FString Ext = FPaths::GetExtension(FileName, false).ToLower();

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("file_name"), FileName);
	Body->SetStringField(TEXT("kind"), TEXT("image"));
	Body->SetStringField(TEXT("extension"), Ext.IsEmpty() ? TEXT("png") : Ext);

	FString BodyStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	if (!FJsonSerializer::Serialize(Body.ToSharedRef(), Writer))
	{
		UE_LOG(LogVPSplat, Error, TEXT("PostPrepareUpload: JSON serialize failed for %s."), *FullImagePath);
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload JSON serialize failed"));
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/media-assets:prepare_upload"), *GetMarbleBaseURL());
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	ApplyMarbleJsonHeaders(Request);
	Request->SetContentAsString(BodyStr);

	UE_LOG(LogVPSplat, Log, TEXT("PostPrepareUpload: POST %s (file %s)."), *Url, *FileName);

	Request->OnProcessRequestComplete().BindUObject(this, &UWorldLabsAPIClient::OnPrepareUploadResponse);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogVPSplat, Error, TEXT("PostPrepareUpload: ProcessRequest failed for %s."), *FullImagePath);
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload ProcessRequest failed"));
	}
}

void UWorldLabsAPIClient::OnPrepareUploadResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	const int32 Code = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : -1;
	const FString Content = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
	UE_LOG(LogVPSplat, Warning, TEXT("OnPrepareUploadResponse: HTTP %d body: %s"), Code, *Content);

	if (!bSucceeded || !HttpResponse.IsValid() || Code < 200 || Code > 299)
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("prepare_upload failed: HTTP %d — %s"), Code, *Content));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("prepare_upload invalid JSON: %s"), *Content));
		return;
	}

	const TSharedPtr<FJsonObject>* MediaAssetPtr = nullptr;
	if (!Root->TryGetObjectField(TEXT("media_asset"), MediaAssetPtr) || !MediaAssetPtr || !MediaAssetPtr->IsValid())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload: missing media_asset"));
		return;
	}
	const TSharedPtr<FJsonObject>& MediaAssetObj = *MediaAssetPtr;
	if (!MediaAssetObj->TryGetStringField(TEXT("media_asset_id"), StagedMediaAssetId) || StagedMediaAssetId.IsEmpty())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload: missing media_asset_id"));
		return;
	}

	const TSharedPtr<FJsonObject>* UploadInfoPtr = nullptr;
	if (!Root->TryGetObjectField(TEXT("upload_info"), UploadInfoPtr) || !UploadInfoPtr || !UploadInfoPtr->IsValid())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload: missing upload_info"));
		return;
	}
	const TSharedPtr<FJsonObject>& UploadInfoObj = *UploadInfoPtr;
	if (!UploadInfoObj->TryGetStringField(TEXT("upload_url"), StagedGCSUploadUrl) || StagedGCSUploadUrl.IsEmpty())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("prepare_upload: missing upload_url"));
		return;
	}

	StagedGCSRequiredHeaders.Reset();
	const TSharedPtr<FJsonObject>* ReqHeadersPtr = nullptr;
	if (UploadInfoObj->TryGetObjectField(TEXT("required_headers"), ReqHeadersPtr) && ReqHeadersPtr && ReqHeadersPtr->IsValid())
	{
		StagedGCSRequiredHeaders = *ReqHeadersPtr;
	}

	UE_LOG(LogVPSplat, Log, TEXT("OnPrepareUploadResponse: media_asset_id=%s — uploading to GCS."), *StagedMediaAssetId);
	IssueGCSUpload(StagedGCSUploadUrl, PendingLocalUploadPath, StagedGCSRequiredHeaders);
}

void UWorldLabsAPIClient::IssueGCSUpload(const FString& UploadUrl, const FString& FullFilePath, TSharedPtr<FJsonObject> RequiredHeadersJson)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FullFilePath))
	{
		UE_LOG(LogVPSplat, Error, TEXT("IssueGCSUpload: failed to read %s"), *FullFilePath);
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("GCS: failed to read %s"), *FullFilePath));
		return;
	}

	const int32 FileBytes = FileData.Num();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(UploadUrl);
	Request->SetVerb(TEXT("PUT"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("image/png"));
	if (RequiredHeadersJson.IsValid())
	{
		for (const auto& Pair : RequiredHeadersJson->Values)
		{
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
			{
				Request->SetHeader(Pair.Key, Pair.Value->AsString());
			}
		}
	}
	Request->SetContent(MoveTemp(FileData));

	UE_LOG(LogVPSplat, Log, TEXT("IssueGCSUpload: PUT (no WLT-Api-Key) %s (%d bytes)."), *UploadUrl, FileBytes);

	Request->OnProcessRequestComplete().BindUObject(this, &UWorldLabsAPIClient::OnGCSUploadResponse);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogVPSplat, Error, TEXT("IssueGCSUpload: ProcessRequest failed."));
		OnWorldFailed.ExecuteIfBound(TEXT("GCS ProcessRequest failed"));
	}
}

void UWorldLabsAPIClient::OnGCSUploadResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	const int32 Code = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : -1;
	const FString Content = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
	UE_LOG(LogVPSplat, Warning, TEXT("OnGCSUploadResponse: HTTP %d (%s)"), Code, *Content);

	if (!bSucceeded || !HttpResponse.IsValid() || Code < 200 || Code > 299)
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("GCS upload failed: HTTP %d — %s"), Code, *Content));
		return;
	}

	UE_LOG(LogVPSplat, Log, TEXT("OnGCSUploadResponse: uploaded media_asset_id %s — submitting worlds:generate (panorama)."), *StagedMediaAssetId);
	SubmitGenerationRequest(StagedMediaAssetId);
}

void UWorldLabsAPIClient::SubmitGenerationRequest(const FString& MediaAssetId)
{
	TSharedPtr<FJsonObject> ImagePrompt = MakeShared<FJsonObject>();
	ImagePrompt->SetStringField(TEXT("source"), TEXT("media_asset"));
	ImagePrompt->SetStringField(TEXT("media_asset_id"), MediaAssetId);
	ImagePrompt->SetBoolField(TEXT("is_pano"), true);

	static const FString SystemInstruction = TEXT(
		"CRITICAL: This 360 panorama contains white/grey primitive "
		"placeholder shapes — cones, pyramids, cylinders, boxes, "
		"spheres, and flat planes. These are spatial layout guides ONLY. "
		"You MUST NOT reproduce any of these shapes in the output. "
		"Do not turn them into rocks, ruins, columns, trees, or any "
		"recognizable physical object that echoes their primitive form. "
		"Replace each primitive's location with organic natural or "
		"architectural environment that fits the chosen environment "
		"preset and mood. The final world must look like it was "
		"photographed in the real world with no trace of 3D primitives."
	);
	const FString NegativePrompt = TEXT(
		"primitive shapes, white geometry, 3D placeholders, "
		"cones, cylinders, boxes, spheres, grey surfaces, "
		"unlit geometry, flat shading, CG render"
	);
	const FString FinalPrompt = PendingGenerationPrompt.IsEmpty()
		? SystemInstruction
		: PendingGenerationPrompt + TEXT("\n\n") + SystemInstruction;

	TSharedPtr<FJsonObject> WP = MakeShared<FJsonObject>();
	WP->SetStringField(TEXT("type"), TEXT("image"));
	WP->SetObjectField(TEXT("image_prompt"), ImagePrompt);
	WP->SetStringField(TEXT("text_prompt"), FinalPrompt);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("display_name"), TEXT("VirtualProductionSplat"));
	Root->SetStringField(TEXT("model"), PendingModelName);
	Root->SetObjectField(TEXT("world_prompt"), WP);
	Root->SetStringField(TEXT("negative_prompt"), NegativePrompt);

	FString BodyStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		OnWorldFailed.ExecuteIfBound(TEXT("worlds:generate JSON serialize failed"));
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/worlds:generate"), *GetMarbleBaseURL());
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	ApplyMarbleJsonHeaders(Request);
	Request->SetContentAsString(BodyStr);

	UE_LOG(LogVPSplat, Log, TEXT("SubmitGenerationRequest: POST %s"), *Url);

	Request->OnProcessRequestComplete().BindUObject(this, &UWorldLabsAPIClient::OnGenerationResponse);

	if (!Request->ProcessRequest())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("worlds:generate ProcessRequest failed"));
	}
}

void UWorldLabsAPIClient::OnGenerationResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	const int32 Code = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : -1;
	const FString Content = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
	UE_LOG(LogVPSplat, Warning, TEXT("OnGenerationResponse: HTTP %d — %s"), Code, *Content);

	if (!bSucceeded || !HttpResponse.IsValid() || Code < 200 || Code > 299)
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("worlds:generate failed: HTTP %d — %s"), Code, *Content));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("worlds:generate invalid JSON: %s"), *Content));
		return;
	}

	FString OpId;
	if (!Root->TryGetStringField(TEXT("operation_id"), OpId) || OpId.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* OpPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("operation"), OpPtr) && OpPtr && OpPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& OpObj = *OpPtr;
			OpObj->TryGetStringField(TEXT("operation_id"), OpId);
			if (OpId.IsEmpty())
			{
				OpObj->TryGetStringField(TEXT("name"), OpId);
			}
		}
	}

	if (OpId.IsEmpty())
	{
		Root->TryGetStringField(TEXT("name"), OpId);
	}

	if (OpId.IsEmpty())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("worlds:generate: missing operation_id"));
		return;
	}

	CurrentOperationID = OpId;
	CurrentStatus = TEXT("operation_pending");
	UE_LOG(LogVPSplat, Log, TEXT("OnGenerationResponse: operation_id=%s — starting poll."), *CurrentOperationID);
	StartPolling(CurrentOperationID);
}

void UWorldLabsAPIClient::PollJobStatus(const FString& OperationID)
{
	const FString Id = OperationID.IsEmpty() ? CurrentOperationID : OperationID;
	PollOperation(Id);
}

void UWorldLabsAPIClient::PollOperation(const FString& OperationID)
{
	if (OperationID.IsEmpty())
	{
		UE_LOG(LogVPSplat, Warning, TEXT("PollOperation: empty operation id."));
		return;
	}
	if (APIKey.IsEmpty())
	{
		Init();
	}
	if (APIKey.IsEmpty())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("API key missing"));
		return;
	}

	const FString Url = FString::Printf(TEXT("%s/operations/%s"), *GetMarbleBaseURL(), *OperationID);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("WLT-Api-Key"), GetAuthHeaderValue());

	UE_LOG(LogVPSplat, Verbose, TEXT("PollOperation: GET %s"), *Url);

	Request->OnProcessRequestComplete().BindUObject(this, &UWorldLabsAPIClient::OnPollOperationResponse);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogVPSplat, Error, TEXT("PollOperation: ProcessRequest failed."));
	}
}

void UWorldLabsAPIClient::OnPollOperationResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	const int32 Code = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : -1;
	const FString Content = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
	UE_LOG(LogVPSplat, Warning, TEXT("OnPollOperationResponse: HTTP %d — %s"), Code, *Content);

	if (!bSucceeded || !HttpResponse.IsValid() || Code < 200 || Code > 299)
	{
		StopPolling();
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("Poll operation failed: HTTP %d — %s"), Code, *Content));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("Poll: invalid JSON"));
		return;
	}

	bool bDone = false;
	Root->TryGetBoolField(TEXT("done"), bDone);

	FString StatusStr;
	Root->TryGetStringField(TEXT("status"), StatusStr);
	CurrentStatus = StatusStr.IsEmpty() ? (bDone ? TEXT("done") : TEXT("running")) : StatusStr;
	OnPollTick.ExecuteIfBound(CurrentOperationID, CurrentStatus);

	if (!bDone)
	{
		UE_LOG(LogVPSplat, Log, TEXT("OnPollOperationResponse: not done yet (status=%s)."), *CurrentStatus);
		return;
	}

	FString WorldId;
	const TSharedPtr<FJsonObject>* ResponsePtr = nullptr;
	if (Root->TryGetObjectField(TEXT("response"), ResponsePtr) && ResponsePtr && ResponsePtr->IsValid())
	{
		(*ResponsePtr)->TryGetStringField(TEXT("world_id"), WorldId);
	}
	if (WorldId.IsEmpty())
	{
		StopPolling();
		OnWorldFailed.ExecuteIfBound(TEXT("Operation done but world_id missing in response"));
		return;
	}

	CurrentWorldId = WorldId;

	StopPolling();
	LastWorldId = WorldId;
	LastWorldUrl = FString::Printf(TEXT("https://marble.worldlabs.ai/world/%s"), *WorldId);
	UE_LOG(LogVPSplat, Log, TEXT("OnPollOperationResponse: done — world_id=%s"), *WorldId);
	FetchWorldAsset(WorldId);
}

void UWorldLabsAPIClient::FetchWorldAsset(const FString& WorldID)
{
	const FString Url = FString::Printf(TEXT("%s/worlds/%s"), *GetMarbleBaseURL(), *WorldID);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("WLT-Api-Key"), GetAuthHeaderValue());

	UE_LOG(LogVPSplat, Log, TEXT("FetchWorldAsset: GET %s"), *Url);

	Request->OnProcessRequestComplete().BindUObject(this, &UWorldLabsAPIClient::OnFetchWorldResponse);

	if (!Request->ProcessRequest())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("fetch world ProcessRequest failed"));
	}
}

void UWorldLabsAPIClient::OnFetchWorldResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	const int32 Code = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : -1;
	const FString Content = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
	UE_LOG(LogVPSplat, Warning, TEXT("OnFetchWorldResponse: HTTP %d — %s"), Code, *Content);

	if (!bSucceeded || !HttpResponse.IsValid() || Code < 200 || Code > 299)
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("GET world failed: HTTP %d — %s"), Code, *Content));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OnWorldFailed.ExecuteIfBound(TEXT("GET world: invalid JSON"));
		return;
	}

	// Prefer SPZ downloads (world assets return .spz URLs).
	FString SpzUrl;
	if (TryExtractSpzDownloadUrl(Root, SpzUrl, TEXT("500k")))
	{
		const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("GaussianSplats")));
		IFileManager::Get().MakeDirectory(*ContentDir, true);

		const FString WorldIdForName = CurrentWorldId.IsEmpty() ? TEXT("unknown_world") : CurrentWorldId;
		const FString SavePath = FPaths::Combine(ContentDir, FString::Printf(TEXT("WorldLabs_%s_500k.spz"), *WorldIdForName));

		UE_LOG(LogVPSplat, Log, TEXT("OnFetchWorldResponse: SPZ URL resolved (saving to %s)."), *SavePath);

		// Keep OnWorldReady for UI/debug/status; auto-download happens immediately after.
		OnWorldReady.ExecuteIfBound(SpzUrl);
		DownloadSPZFile(SpzUrl, SavePath);

		CurrentStatus = TEXT("complete");
		return;
	}

	// Fallback: old/alternate response may still return .ply.
	FString PlyUrl;
	if (!TryExtractPlyDownloadUrl(Root, PlyUrl))
	{
		OnWorldFailed.ExecuteIfBound(FString::Printf(TEXT("Could not find .spz or .ply download URL in world JSON: %s"), *Content));
		return;
	}

	CurrentStatus = TEXT("complete");
	UE_LOG(LogVPSplat, Log, TEXT("OnFetchWorldResponse: PLY URL resolved."));
	OnWorldReady.ExecuteIfBound(PlyUrl);
}

void UWorldLabsAPIClient::StartPolling(const FString& OperationID)
{
	StopPolling();
	PollingOperationID = OperationID;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogVPSplat, Error, TEXT("StartPolling: GetWorld() is null — outer UWorldLabsAPIClient to an Actor."));
		return;
	}

	TWeakObjectPtr<UWorldLabsAPIClient> WeakThis(this);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	const FString IdCopy = OperationID;

	World->GetTimerManager().SetTimer(
		PollTimerHandle,
		[WeakThis, WeakWorld, IdCopy]()
		{
			UWorld* W = WeakWorld.Get();
			UWorldLabsAPIClient* Self = WeakThis.Get();
			if (W && Self)
			{
				Self->PollJobStatus(IdCopy);
			}
		},
		PollIntervalSeconds,
		true);

	UE_LOG(LogVPSplat, Log, TEXT("StartPolling: every %.1fs for operation %s"), PollIntervalSeconds, *OperationID);
	PollJobStatus(OperationID);
}

void UWorldLabsAPIClient::StopPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
	PollTimerHandle.Invalidate();
	PollingOperationID.Reset();
}

void UWorldLabsAPIClient::DownloadPLYFile(const FString& URL, const FString& SavePath)
{
	if (URL.IsEmpty())
	{
		UE_LOG(LogVPSplat, Error, TEXT("DownloadPLYFile: empty URL."));
		return;
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(SavePath);
	const FString Dir = FPaths::GetPath(FullPath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));

	TWeakObjectPtr<UWorldLabsAPIClient> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, FullPath](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
	{
		if (UWorldLabsAPIClient* Self = WeakThis.Get())
		{
			Self->OnDownloadResponse(Req, Res, bOk, FullPath);
		}
	});

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogVPSplat, Error, TEXT("DownloadPLYFile: ProcessRequest failed."));
	}
	else
	{
		UE_LOG(LogVPSplat, Log, TEXT("DownloadPLYFile: GET %s → %s"), *URL, *FullPath);
	}
}

void UWorldLabsAPIClient::DownloadSPZFile(const FString& URL, const FString& SavePath)
{
	if (URL.IsEmpty())
	{
		UE_LOG(LogVPSplat, Error, TEXT("DownloadSPZFile: empty URL."));
		return;
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(SavePath);
	const FString Dir = FPaths::GetPath(FullPath);
	IFileManager::Get().MakeDirectory(*Dir, true);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));

	TWeakObjectPtr<UWorldLabsAPIClient> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, FullPath](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
	{
		if (UWorldLabsAPIClient* Self = WeakThis.Get())
		{
			Self->OnDownloadSPZResponse(Req, Res, bOk, FullPath);
		}
	});

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogVPSplat, Error, TEXT("DownloadSPZFile: ProcessRequest failed."));
		return;
	}

	UE_LOG(LogVPSplat, Log, TEXT("DownloadSPZFile: GET %s -> %s"), *URL, *FullPath);
}

void UWorldLabsAPIClient::OnDownloadSPZResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded, FString SavePath)
{
	if (!bSucceeded || !Response.IsValid())
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadSPZResponse: request failed."));
		OnWorldFailed.ExecuteIfBound(TEXT("SPZ download request failed"));
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code < 200 || Code > 299)
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadSPZResponse: HTTP %d"), Code);
		OnWorldFailed.ExecuteIfBound(TEXT("SPZ download HTTP failed"));
		return;
	}

	const TArray<uint8>& Bytes = Response->GetContent();
	if (Bytes.Num() == 0)
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadSPZResponse: empty body."));
		OnWorldFailed.ExecuteIfBound(TEXT("SPZ download empty body"));
		return;
	}

	if (FFileHelper::SaveArrayToFile(Bytes, *SavePath))
	{
		UE_LOG(LogVPSplat, Warning, TEXT("OnDownloadSPZResponse: saved %d bytes to %s"), Bytes.Num(), *SavePath);
		CurrentStatus = TEXT("spz_downloaded");
		OnSplatDownloaded.ExecuteIfBound(SavePath);
	}
	else
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadSPZResponse: failed to write %s"), *SavePath);
		OnWorldFailed.ExecuteIfBound(TEXT("SPZ file write failed"));
	}
}

void UWorldLabsAPIClient::OnDownloadResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FString SavePath)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadResponse: request failed."));
		return;
	}
	const int32 Code = Response->GetResponseCode();
	if (Code < 200 || Code > 299)
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadResponse: HTTP %d"), Code);
		return;
	}

	const TArray<uint8>& Bytes = Response->GetContent();
	if (Bytes.Num() == 0)
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadResponse: empty body."));
		return;
	}

	if (FFileHelper::SaveArrayToFile(Bytes, *SavePath))
	{
		UE_LOG(LogVPSplat, Log, TEXT("OnDownloadResponse: saved %d bytes to %s"), Bytes.Num(), *SavePath);
	}
	else
	{
		UE_LOG(LogVPSplat, Error, TEXT("OnDownloadResponse: failed to write %s"), *SavePath);
	}
}

bool UWorldLabsAPIClient::TryExtractPlyDownloadUrlFromValue(const TSharedPtr<FJsonValue>& Val, FString& OutUrl)
{
	if (!Val.IsValid())
	{
		return false;
	}
	switch (Val->Type)
	{
	case EJson::Object:
		return TryExtractPlyDownloadUrl(Val->AsObject(), OutUrl);
	case EJson::Array:
		for (const TSharedPtr<FJsonValue>& E : Val->AsArray())
		{
			if (TryExtractPlyDownloadUrlFromValue(E, OutUrl))
			{
				return true;
			}
		}
		return false;
	case EJson::String:
	{
		const FString S = Val->AsString();
		if (S.StartsWith(TEXT("http")) && (S.Contains(TEXT(".ply")) || S.Contains(TEXT("ply?")) || S.Contains(TEXT("/ply"))))
		{
			OutUrl = S;
			return true;
		}
		return false;
	}
	default:
		return false;
	}
}

bool UWorldLabsAPIClient::TryExtractPlyDownloadUrl(const TSharedPtr<FJsonObject>& Root, FString& OutUrl)
{
	if (!Root.IsValid())
	{
		return false;
	}
	static const TCHAR* Keys[] = {
		TEXT("ply_url"), TEXT("splat_url"), TEXT("download_url"), TEXT("asset_url"), TEXT("signed_url"), TEXT("url")};
	for (const TCHAR* K : Keys)
	{
		FString S;
		if (Root->TryGetStringField(K, S) && S.StartsWith(TEXT("http")) &&
			(S.Contains(TEXT(".ply")) || S.Contains(TEXT("ply?")) || S.Contains(TEXT("/ply"))))
		{
			OutUrl = S;
			return true;
		}
	}
	for (const auto& Pair : Root->Values)
	{
		if (TryExtractPlyDownloadUrlFromValue(Pair.Value, OutUrl))
		{
			return true;
		}
	}
	return false;
}

bool UWorldLabsAPIClient::TryExtractSpzDownloadUrl(const TSharedPtr<FJsonObject>& Root, FString& OutUrl, const FString& DesiredKey)
{
	if (!Root.IsValid())
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: root invalid."));
		return false;
	}

	UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: checking assets field..."));
	const TSharedPtr<FJsonObject>* AssetsPtr = nullptr;
	if (!Root->TryGetObjectField(TEXT("assets"), AssetsPtr) || !AssetsPtr || !AssetsPtr->IsValid())
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: assets missing/invalid."));
		return false;
	}

	UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: checking splats field..."));
	const TSharedPtr<FJsonObject>* SplatsPtr = nullptr;
	if (!(*AssetsPtr)->TryGetObjectField(TEXT("splats"), SplatsPtr) || !SplatsPtr || !SplatsPtr->IsValid())
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: splats missing/invalid."));
		return false;
	}

	UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: checking spz_urls field..."));
	const TSharedPtr<FJsonObject>* SpzUrlsPtr = nullptr;
	if (!(*SplatsPtr)->TryGetObjectField(TEXT("spz_urls"), SpzUrlsPtr) || !SpzUrlsPtr || !SpzUrlsPtr->IsValid())
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: spz_urls missing/invalid."));
		return false;
	}

	const FString KeyToTry = DesiredKey.IsEmpty() ? TEXT("500k") : DesiredKey;
	UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: checking spz_urls['%s'] field..."), *KeyToTry);

	FString Url;
	if (!(*SpzUrlsPtr)->TryGetStringField(KeyToTry, Url))
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: key '%s' missing."), *KeyToTry);
		return false;
	}

	UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: %s = %s"), *KeyToTry, *Url);

	if (!Url.StartsWith(TEXT("http")))
	{
		UE_LOG(LogVPSplat, Warning, TEXT("ExtractURL: extracted value is not a URL."));
		return false;
	}

	OutUrl = Url;
	return true;
}
