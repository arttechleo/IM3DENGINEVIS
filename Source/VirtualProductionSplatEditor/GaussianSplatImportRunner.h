// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GaussianSplatImportRunner.generated.h"

class AActor;

/**
 * Editor-placed actor: spawn a CS Gaussian splat from a .ply path (e.g. after WorldLabs download to Content/GaussianSplats).
 */
UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API AGaussianSplatImportRunner : public AActor
{
	GENERATED_BODY()

public:
	AGaussianSplatImportRunner();

	/** Absolute or project-relative path to .ply (e.g. .../Content/GaussianSplats/WorldLabs_job.ply) */
	UPROPERTY(EditAnywhere, Category = "VP Pipeline|Gaussian")
	FString PLYFilePath;

	/** Uniform actor scale after spawn (MLSLabs AGaussianSplattingActor). */
	UPROPERTY(EditAnywhere, Category = "VP Pipeline|Gaussian", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "5.0"))
	float SplatScale = 1.f;

	/** If true, spawn at this actor's transform; if false, use SpawnLocation/SpawnRotation */
	UPROPERTY(EditAnywhere, Category = "VP Pipeline|Gaussian")
	bool bUseThisActorTransform = true;

	UPROPERTY(EditAnywhere, Category = "VP Pipeline|Gaussian", meta = (EditCondition = "!bUseThisActorTransform"))
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "VP Pipeline|Gaussian", meta = (EditCondition = "!bUseThisActorTransform"))
	FRotator SpawnRotation = FRotator::ZeroRotator;

	UFUNCTION(CallInEditor, Category = "VP Pipeline|Gaussian")
	void ImportPLYIntoLevel();

	UPROPERTY(VisibleAnywhere, Category = "VP Pipeline|Gaussian")
	TObjectPtr<AActor> LastSpawnedSplat;

private:
	FTransform ComputeSpawnTransform() const;
	static void PostImportToast(const FString& Message, bool bSuccess);
};
