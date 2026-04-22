// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class UTextureRenderTarget2D;

#include "GreyboxExporter.generated.h"

/**
 * Legacy UObject: finds APanoramicCapture360 in the level and runs CaptureFaces().
 * Prefer APanoramicExportRunner in the editor.
 */
UCLASS(BlueprintType)
class VIRTUALPRODUCTIONSPLAT_API UGreyboxExporter : public UObject
{
	GENERATED_BODY()

public:
	UGreyboxExporter();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "VP Pipeline")
	void ExportAllCameras();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VP Pipeline")
	FString OutputDirectory;

	UPROPERTY(BlueprintReadOnly, Category = "VP Pipeline")
	TArray<FString> LastExportedFilePaths;

	UFUNCTION(BlueprintPure, Category = "VP Pipeline")
	TArray<FString> GetLastExportedFilePaths() const { return LastExportedFilePaths; }

private:
	bool SaveRenderTargetToPNG(UTextureRenderTarget2D* RT, const FString& FilePath);
};
