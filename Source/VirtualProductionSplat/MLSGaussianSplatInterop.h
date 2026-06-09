// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UObject;
class UWorld;
class UGaussianSplatAsset;

/**
 * Thin bridge from project pipeline code to the NanoGS plugin.
 *
 * NanoGS ships real public headers (NANOGS_API), so there is no reflection here:
 * a .ply is parsed with FPLYFileReader into a transient UGaussianSplatAsset, then an
 * AGaussianSplatActor is spawned and pointed at that asset via UGaussianSplatComponent.
 *
 * (Type name kept as FMLSGaussianSplatInterop for source compatibility with existing callers.)
 */
struct VIRTUALPRODUCTIONSPLAT_API FMLSGaussianSplatInterop
{
	/** True if the NanoGS runtime module is loaded (loads it on demand). OutError filled otherwise. */
	static bool EnsureMLSLabsRendererReady(FString& OutError);

	/**
	 * Reads a .ply via FPLYFileReader and builds a transient UGaussianSplatAsset.
	 * @param Outer Owning object for the new asset (defaults to the transient package if null).
	 * @return nullptr on failure (OutError filled).
	 */
	static UGaussianSplatAsset* CreateSplatAssetFromPly(const FString& AbsolutePlyPath, UObject* Outer, FString& OutError);

	/**
	 * Spawns (or, when bReuseWorldLabsSingleton, reuses an actor labelled "WorldLabs_Splat") an
	 * AGaussianSplatActor at WorldTransform, assigns Asset, and sets the component SplatScale.
	 * @return nullptr on failure (OutError filled).
	 */
	static AActor* SpawnActorWithAsset(
		UWorld* World,
		UGaussianSplatAsset* Asset,
		float UniformScale,
		const FTransform& WorldTransform,
		bool bReuseWorldLabsSingleton,
		FString& OutError);

	/** Creates an asset from the .ply and spawns a fresh AGaussianSplatActor at WorldTransform. */
	static AActor* SpawnGaussianSplatAt(
		UWorld* World,
		const FString& AbsolutePlyPath,
		const FTransform& WorldTransform,
		float UniformScale,
		FString& OutError);

	/** Find/reload the "WorldLabs_Splat" actor or spawn a new one at SpawnLocation. */
	static AActor* SpawnOrReloadWorldLabsSplat(
		UWorld* World,
		const FString& AbsolutePlyPath,
		float UniformScale,
		const FVector& SpawnLocation,
		FString& OutError);

	/** True if any AGaussianSplatActor exists in the world. */
	static bool WorldHasMLSGaussianActor(UWorld* World);
};
