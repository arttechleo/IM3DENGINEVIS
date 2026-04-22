// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldLabsImageHelper.h"
#include "VirtualProductionSplat.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"

FString FWorldLabsImageHelper::EncodeFileToBase64(const FString& FilePath)
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);
	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *FullPath))
	{
		UE_LOG(LogVPSplat, Error, TEXT("WorldLabsImageHelper: failed to read file: %s"), *FullPath);
		return FString();
	}
	return FBase64::Encode(FileBytes);
}

FString FWorldLabsImageHelper::GetMimeType(const FString& FilePath)
{
	const FString Ext = FPaths::GetExtension(FilePath).ToLower();
	if (Ext == TEXT("png"))
	{
		return TEXT("image/png");
	}
	if (Ext == TEXT("jpg") || Ext == TEXT("jpeg"))
	{
		return TEXT("image/jpeg");
	}
	if (Ext == TEXT("webp"))
	{
		return TEXT("image/webp");
	}
	if (Ext == TEXT("gif"))
	{
		return TEXT("image/gif");
	}
	if (Ext == TEXT("bmp"))
	{
		return TEXT("image/bmp");
	}
	if (Ext == TEXT("tga"))
	{
		return TEXT("image/tga");
	}
	return TEXT("application/octet-stream");
}
