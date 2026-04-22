// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FWorldLabsImageHelper
{
	// Reads a file from disk and returns its contents as a Base64-encoded FString.
	// Returns empty string and logs an error on failure.
	static FString EncodeFileToBase64(const FString& FilePath);

	// Returns the MIME type string for a given file extension (.png → "image/png", etc.)
	static FString GetMimeType(const FString& FilePath);
};
