// Copyright Epic Games, Inc. All Rights Reserved.

#include "GaussianSplatImportHelper.h"
#include "MLSGaussianSplatInterop.h"
#include "VirtualProductionSplat.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

AActor* UGaussianSplatImportHelper::SpawnGaussianSplatAt(
	UObject* WorldContextObject,
	const FString& AbsolutePLYPath,
	const FTransform& WorldTransform,
	float SplatScale)
{
	if (AbsolutePLYPath.IsEmpty())
	{
		UE_LOG(LogVPSplat, Error, TEXT("SpawnGaussianSplatAt: empty PLY path."));
		return nullptr;
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(AbsolutePLYPath);
	if (!FPaths::FileExists(FullPath))
	{
		UE_LOG(LogVPSplat, Error, TEXT("SpawnGaussianSplatAt: file not found: %s"), *FullPath);
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		UE_LOG(LogVPSplat, Error, TEXT("SpawnGaussianSplatAt: no world from context."));
		return nullptr;
	}

	FString Err;
	if (AActor* Actor = FMLSGaussianSplatInterop::SpawnGaussianSplatAt(World, FullPath, WorldTransform, SplatScale, Err))
	{
		UE_LOG(LogVPSplat, Log, TEXT("SpawnGaussianSplatAt: loaded %s"), *FullPath);
		return Actor;
	}

	UE_LOG(LogVPSplat, Error, TEXT("SpawnGaussianSplatAt: %s"), *Err);
	return nullptr;
}

bool UGaussianSplatImportHelper::WorldHasMLSGaussianActorInWorld(UWorld* World)
{
	return FMLSGaussianSplatInterop::WorldHasMLSGaussianActor(World);
}

AActor* UGaussianSplatImportHelper::SpawnOrReloadWorldLabsSplatInWorld(
	UWorld* World,
	const FString& AbsolutePlyPath,
	float UniformScale,
	const FVector& SpawnLocation,
	FString& OutError)
{
	return FMLSGaussianSplatInterop::SpawnOrReloadWorldLabsSplat(
		World, AbsolutePlyPath, UniformScale, SpawnLocation, OutError);
}
