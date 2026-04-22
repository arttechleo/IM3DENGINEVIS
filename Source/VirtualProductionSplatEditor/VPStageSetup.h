// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VPStageSetup.generated.h"

class AActor;
class ACineCameraActor;
class APostProcessVolume;

/**
 * Editor workflow: locate VP camera, add simple fill lighting + post, log summary.
 * Assign GaussianSplatActor for tracking / logging (optional for steps).
 */
UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API AVPStageSetup : public AActor
{
	GENERATED_BODY()

public:
	AVPStageSetup();

	UPROPERTY(EditAnywhere, Category = "VP Stage")
	TObjectPtr<AActor> GaussianSplatActor;

	/** If set, used as the VP camera; otherwise Step1 searches for label "PrimaryCamera" or first cine camera. */
	UPROPERTY(EditAnywhere, Category = "VP Stage")
	TObjectPtr<ACineCameraActor> VpCameraOverride;

	UPROPERTY(VisibleAnywhere, Category = "VP Stage")
	TObjectPtr<ACineCameraActor> CachedVpCamera;

	UFUNCTION(CallInEditor, Category = "VP Stage")
	void Step1_FindCamera();

	UFUNCTION(CallInEditor, Category = "VP Stage")
	void Step2_AddFillLights();

	UFUNCTION(CallInEditor, Category = "VP Stage")
	void Step3_AddPostProcess();

	UFUNCTION(CallInEditor, Category = "VP Stage")
	void Step4_LogStageSummary();

private:
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedFillActors;

	UPROPERTY()
	TObjectPtr<APostProcessVolume> PostProcessVolume;
};
