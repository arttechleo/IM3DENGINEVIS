// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UActorComponent;
class UClass;
class UWorld;

/**
 * Win64: MLSLabsRenderer Pro ships without Public headers in the release ZIP.
 * These helpers locate AGaussianSplattingActor / UGaussianSplattingComponent via reflection
 * and drive loading using documented property "SplatDataPath" + QueueLoadSplatData().
 */
struct FMLSGaussianSplatInterop
{
	static UClass* GetGaussianSplattingActorClass();
	static UClass* GetGaussianSplattingComponentClass();

	static UActorComponent* FindGaussianSplattingComponent(AActor* Actor);

	/** Sets FString property on the component if present (tries SplatDataPath, then SplatFileName). */
	static bool SetPrimaryPlyPathProperty(UActorComponent* GaussianComp, const FString& AbsolutePlyPath);

	static bool QueueLoadSplatData(UActorComponent* GaussianComp);
	static bool RefreshBoundsFromLoadedSplat(UActorComponent* GaussianComp);

	/**
	 * Spawns AGaussianSplattingActor at WorldTransform, sets uniform world scale, assigns PLY path, queues load.
	 * @return nullptr on failure (OutError filled).
	 */
	static AActor* SpawnGaussianSplatAt(
		UWorld* World,
		const FString& AbsolutePlyPath,
		const FTransform& WorldTransform,
		float UniformScale,
		FString& OutError);

	/**
	 * Finds an actor labelled WorldLabs_Splat of the MLS Gaussian class, or spawns a new one at SpawnLocation.
	 * Updates path + reload on existing actor.
	 */
	static AActor* SpawnOrReloadWorldLabsSplat(
		UWorld* World,
		const FString& AbsolutePlyPath,
		float UniformScale,
		const FVector& SpawnLocation,
		FString& OutError);

	/** True if any AGaussianSplattingActor exists in the world. */
	static bool WorldHasMLSGaussianActor(UWorld* World);
};
