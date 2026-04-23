// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClaudePromptRefiner.h"
#include "VirtualProductionSplat.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"

static const TCHAR* GClaudeSystemPrompt = TEXT(
	"You are a world-building prompt engineer for a spatial AI pipeline. "
	"You receive a 360-degree equirectangular panorama of a UE5 greybox scene made of white primitive shapes, "
	"plus the user's creative intent and optional style reference images. "
	"Output a single cinematically precise prompt for WorldLabs Marble API that: "
	"maps each primitive to its real-world equivalent (cones=mountains/terrain, cylinders=columns/trees, "
	"boxes=walls/buildings, flat planes=ground/water), incorporates the user's mood and style intent, "
	"specifies lighting, time of day, atmosphere, and material quality. "
	"Output ONLY the prompt text. No explanation. Max 350 words."
);

static TSharedPtr<FJsonObject> MakeBase64ImageContent(const TArray<uint8>& RawData, const FString& MediaType)
{
	TSharedPtr<FJsonObject> SourceObj = MakeShared<FJsonObject>();
	SourceObj->SetStringField(TEXT("type"), TEXT("base64"));
	SourceObj->SetStringField(TEXT("media_type"), MediaType);
	SourceObj->SetStringField(TEXT("data"), FBase64::Encode(RawData));

	TSharedPtr<FJsonObject> ContentObj = MakeShared<FJsonObject>();
	ContentObj->SetStringField(TEXT("type"), TEXT("image"));
	ContentObj->SetObjectField(TEXT("source"), SourceObj);
	return ContentObj;
}

void UClaudePromptRefiner::RefinePrompt(
	const FString& AnthropicKey,
	const FString& PanoramaPath,
	const FString& UserIntent,
	const TArray<FString>& RefImagePaths,
	FOnRefinedPrompt OnComplete)
{
	CompletionDelegate = OnComplete;

	TArray<uint8> PanoramaData;
	if (!FFileHelper::LoadFileToArray(PanoramaData, *PanoramaPath))
	{
		UE_LOG(LogVPSplat, Error, TEXT("ClaudePromptRefiner: failed to load panorama: %s"), *PanoramaPath);
		OnComplete.ExecuteIfBound(FString());
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ContentArray;

	// Panorama (required)
	ContentArray.Add(MakeShared<FJsonValueObject>(MakeBase64ImageContent(PanoramaData, TEXT("image/png"))));

	// Optional style reference images
	for (const FString& RefPath : RefImagePaths)
	{
		TArray<uint8> RefData;
		if (FFileHelper::LoadFileToArray(RefData, *RefPath))
		{
			ContentArray.Add(MakeShared<FJsonValueObject>(MakeBase64ImageContent(RefData, TEXT("image/png"))));
		}
	}

	// Text turn
	TSharedPtr<FJsonObject> TextObj = MakeShared<FJsonObject>();
	TextObj->SetStringField(TEXT("type"), TEXT("text"));
	TextObj->SetStringField(TEXT("text"),
		FString::Printf(TEXT("User intent: %s\n\nGenerate the WorldLabs prompt."), *UserIntent));
	ContentArray.Add(MakeShared<FJsonValueObject>(TextObj));

	TSharedPtr<FJsonObject> MessageObj = MakeShared<FJsonObject>();
	MessageObj->SetStringField(TEXT("role"), TEXT("user"));
	MessageObj->SetArrayField(TEXT("content"), ContentArray);

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	MessagesArray.Add(MakeShared<FJsonValueObject>(MessageObj));

	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("model"), TEXT("claude-sonnet-4-6"));
	RootObj->SetNumberField(TEXT("max_tokens"), 1024);
	RootObj->SetStringField(TEXT("system"), FString(GClaudeSystemPrompt));
	RootObj->SetArrayField(TEXT("messages"), MessagesArray);

	FString BodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("https://api.anthropic.com/v1/messages"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("x-api-key"), AnthropicKey);
	Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	Request->SetHeader(TEXT("content-type"), TEXT("application/json"));
	Request->SetContentAsString(BodyStr);
	Request->OnProcessRequestComplete().BindUObject(this, &UClaudePromptRefiner::OnAPIResponse);
	Request->ProcessRequest();

	UE_LOG(LogVPSplat, Log, TEXT("ClaudePromptRefiner: sent request — body %d chars, panorama %d bytes"),
		BodyStr.Len(), PanoramaData.Num());
}

void UClaudePromptRefiner::OnAPIResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogVPSplat, Error, TEXT("ClaudePromptRefiner: HTTP request failed."));
		CompletionDelegate.ExecuteIfBound(FString());
		return;
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();

	if (Code != 200)
	{
		UE_LOG(LogVPSplat, Error, TEXT("ClaudePromptRefiner: API returned %d — %s"), Code, *Body.Left(512));
		CompletionDelegate.ExecuteIfBound(FString());
		return;
	}

	TSharedPtr<FJsonObject> ResponseObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, ResponseObj) || !ResponseObj.IsValid())
	{
		UE_LOG(LogVPSplat, Error, TEXT("ClaudePromptRefiner: JSON parse failed."));
		CompletionDelegate.ExecuteIfBound(FString());
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
	if (ResponseObj->TryGetArrayField(TEXT("content"), ContentArr) && ContentArr && ContentArr->Num() > 0)
	{
		if (TSharedPtr<FJsonObject> First = (*ContentArr)[0]->AsObject())
		{
			FString Text;
			if (First->TryGetStringField(TEXT("text"), Text))
			{
				UE_LOG(LogVPSplat, Log, TEXT("ClaudePromptRefiner: refined prompt (%d chars)"), Text.Len());
				CompletionDelegate.ExecuteIfBound(Text.TrimStartAndEnd());
				return;
			}
		}
	}

	UE_LOG(LogVPSplat, Error, TEXT("ClaudePromptRefiner: could not parse content[0].text — %s"), *Body.Left(512));
	CompletionDelegate.ExecuteIfBound(FString());
}
