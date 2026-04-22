// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MultiAngleCameraRig.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Six 90° FOV scene captures (+X -X +Y -Y +Z -Z), saved as PNGs for Python equirect stitching.
 */
UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLAT_API APanoramicCapture360 : public AActor
{
	GENERATED_BODY()

public:
	APanoramicCapture360();

	/** Resolution of each cube face (square). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "360 Capture")
	int32 FaceResolution = 512;

	/** Output directory for face PNGs (default: Saved/GreyboxExports/faces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "360 Capture")
	FString OutputDirectory;

	/** Capture all 6 faces and save as face_PosX.png, etc. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "360 Capture")
	void CaptureFaces();

	/** Final equirectangular PNG path — set after **StitchPanorama** / Python stitcher (panorama_360.png). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "360 Capture")
	FString LastSavedPath;

	/** Absolute paths to saved face PNGs (filled by CaptureFaces). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "360 Capture")
	TArray<FString> SavedFacePaths;

private:
	/** Six captures: +X -X +Y -Y +Z -Z */
	UPROPERTY()
	TArray<TObjectPtr<USceneCaptureComponent2D>> CaptureComponents;

	bool SaveRenderTargetToPNG(UTextureRenderTarget2D* RT, const FString& FilePath) const;
};
