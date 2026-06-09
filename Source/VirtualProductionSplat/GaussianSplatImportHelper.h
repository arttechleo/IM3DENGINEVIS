// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GaussianSplatImportHelper.generated.h"

class AActor;
class UWorld;

/**
 * Blueprint/editor-facing wrapper. Spawns a NanoGS AGaussianSplatActor from a .ply:
 * the file is parsed into a UGaussianSplatAsset and assigned to the actor's
 * UGaussianSplatComponent (see FMLSGaussianSplatInterop).
 */
UCLASS()
class VIRTUALPRODUCTIONSPLAT_API UGaussianSplatImportHelper : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Spawns an AGaussianSplatActor at WorldTransform and assigns a splat asset built from the .ply.
	 * @param AbsolutePLYPath Full path to .ply (e.g. under Saved/ or project Content/).
	 * @param SplatScale Uniform splat scale, applied to UGaussianSplatComponent::SplatScale (0.1–10).
	 * @return Spawned actor, or nullptr if world missing or load/spawn failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "VP Pipeline|Gaussian", meta = (WorldContext = "WorldContextObject"))
	static AActor* SpawnGaussianSplatAt(
		UObject* WorldContextObject,
		const FString& AbsolutePLYPath,
		const FTransform& WorldTransform,
		float SplatScale = 1.f);

	/** Exported for editor module — true if any NanoGS AGaussianSplatActor exists in the world. */
	static bool WorldHasMLSGaussianActorInWorld(UWorld* World);

	/** Exported for editor module — find/reload "WorldLabs_Splat" or spawn a new NanoGS actor. */
	static AActor* SpawnOrReloadWorldLabsSplatInWorld(
		UWorld* World,
		const FString& AbsolutePlyPath,
		float UniformScale,
		const FVector& SpawnLocation,
		FString& OutError);
};
