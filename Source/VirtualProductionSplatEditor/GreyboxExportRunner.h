// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityActor.h"
#include "GreyboxExportRunner.generated.h"

/**
 * Editor-only: finds APanoramicCapture360 and runs face capture + optional Python equirect stitch.
 */
UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API APanoramicExportRunner : public AEditorUtilityActor
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = "360 Capture")
	void CapturePanorama();

	/** Runs Source/VirtualProductionSplatEditor/StitchEquirectangular.py via Python plugin (or logs manual steps). */
	UFUNCTION(CallInEditor, Category = "360 Capture")
	void StitchPanorama();
};
