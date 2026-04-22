// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GaussianSplatImportHelper.generated.h"

class AActor;
class UWorld;

/**
 * Spawns MLSLabs AGaussianSplattingActor (Win64) and loads a .ply via SplatDataPath + QueueLoadSplatData.
 */
UCLASS()
class VIRTUALPRODUCTIONSPLAT_API UGaussianSplatImportHelper : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Spawns AGaussianSplattingActor at WorldTransform and queues PLY load.
	 * @param AbsolutePLYPath Full path to .ply (e.g. under Saved/ or project Content/).
	 * @param SplatScale Uniform actor scale after spawn (MLSLabs has no separate SplatScale property).
	 * @return Spawned actor, or nullptr if world missing or spawn/load failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "VP Pipeline|Gaussian", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnGaussianSplatAt(
		UObject* WorldContextObject,
		const FString& AbsolutePLYPath,
		const FTransform& WorldTransform,
		float SplatScale = 1.f);

	/** Exported for editor module — detects MLSLabs AGaussianSplattingActor (Win64). */
	static bool WorldHasMLSGaussianActorInWorld(UWorld* World);

	/** Exported for editor module — find/reload WorldLabs_Splat or spawn new MLSLabs actor. */
	static AActor* SpawnOrReloadWorldLabsSplatInWorld(
		UWorld* World,
		const FString& AbsolutePlyPath,
		float UniformScale,
		const FVector& SpawnLocation,
		FString& OutError);
};
