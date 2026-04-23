// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ClaudePromptRefiner.generated.h"

DECLARE_DELEGATE_OneParam(FOnRefinedPrompt, FString);

UCLASS()
class VIRTUALPRODUCTIONSPLAT_API UClaudePromptRefiner : public UObject
{
	GENERATED_BODY()

public:
	void RefinePrompt(
		const FString& AnthropicKey,
		const FString& PanoramaPath,
		const FString& UserIntent,
		const TArray<FString>& RefImagePaths,
		FOnRefinedPrompt OnComplete
	);

private:
	void OnAPIResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
	FOnRefinedPrompt CompletionDelegate;
};
