// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLSGaussianSplatInterop.h"
#include "VirtualProductionSplat.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

// NanoGS public API
#include "GaussianDataTypes.h"
#include "PLYFileReader.h"
#include "GaussianSplatAsset.h"
#include "GaussianSplatActor.h"
#include "GaussianSplatComponent.h"

static const TCHAR* GWorldLabsSplatLabel = TEXT("WorldLabs_Splat");

bool FMLSGaussianSplatInterop::EnsureMLSLabsRendererReady(FString& OutError)
{
	FModuleManager& MM = FModuleManager::Get();
	const FName ModName(TEXT("NanoGS"));
	if (MM.IsModuleLoaded(ModName))
	{
		return true;
	}
	if (MM.LoadModule(ModName))
	{
		return true;
	}
	OutError = TEXT("NanoGS runtime module is not available (check Plugins/NanoGS_UE57 is enabled).");
	UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
	return false;
}

UGaussianSplatAsset* FMLSGaussianSplatInterop::CreateSplatAssetFromPly(
	const FString& AbsolutePlyPath, UObject* Outer, FString& OutError)
{
	TArray<FGaussianSplatData> Splats;
	int32 SHBands = 0;
	if (!FPLYFileReader::ReadPLYFile(AbsolutePlyPath, Splats, OutError, &SHBands))
	{
		// OutError filled by the reader.
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: PLY read failed: %s"), *OutError);
		return nullptr;
	}
	if (Splats.Num() == 0)
	{
		OutError = TEXT("PLY parsed but contained zero splats.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	UObject* AssetOuter = Outer ? Outer : (UObject*)GetTransientPackage();
	UGaussianSplatAsset* Asset = NewObject<UGaussianSplatAsset>(AssetOuter);
	if (!Asset)
	{
		OutError = TEXT("NewObject<UGaussianSplatAsset> failed.");
		return nullptr;
	}

	Asset->InitializeFromSplatData(Splats, EGaussianQualityLevel::VeryHigh);
	UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: built asset with %d splats (SH bands=%d) from %s"),
		Splats.Num(), SHBands, *AbsolutePlyPath);
	return Asset;
}

AActor* FMLSGaussianSplatInterop::SpawnActorWithAsset(
	UWorld* World,
	UGaussianSplatAsset* Asset,
	float UniformScale,
	const FTransform& WorldTransform,
	bool bReuseWorldLabsSingleton,
	FString& OutError)
{
	if (!World || !Asset)
	{
		OutError = TEXT("SpawnActorWithAsset: null world or asset.");
		return nullptr;
	}

	const float CompScale = FMath::Clamp(UniformScale, 0.1f, 10.0f);

	AGaussianSplatActor* TargetActor = nullptr;

#if WITH_EDITOR
	if (bReuseWorldLabsSingleton)
	{
		for (TActorIterator<AGaussianSplatActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == GWorldLabsSplatLabel)
			{
				TargetActor = *It;
				break;
			}
		}
	}
#endif

	if (TargetActor)
	{
		TargetActor->SetActorTransform(FTransform(WorldTransform.GetRotation(), WorldTransform.GetLocation(), FVector::OneVector));
	}
	else
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform SpawnXform(WorldTransform.GetRotation(), WorldTransform.GetLocation(), FVector::OneVector);
		TargetActor = World->SpawnActor<AGaussianSplatActor>(AGaussianSplatActor::StaticClass(), SpawnXform, Params);
		if (!TargetActor)
		{
			OutError = TEXT("SpawnActor(AGaussianSplatActor) failed.");
			UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
			return nullptr;
		}
#if WITH_EDITOR
		if (bReuseWorldLabsSingleton)
		{
			TargetActor->SetActorLabel(GWorldLabsSplatLabel);
		}
#endif
	}

	if (UGaussianSplatComponent* Comp = TargetActor->GaussianSplatComponent)
	{
		Comp->SetSplatAsset(Asset);
		Comp->SplatScale = CompScale;
	}
	else
	{
		OutError = TEXT("AGaussianSplatActor spawned without a GaussianSplatComponent.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	return TargetActor;
}

AActor* FMLSGaussianSplatInterop::SpawnGaussianSplatAt(
	UWorld* World,
	const FString& AbsolutePlyPath,
	const FTransform& WorldTransform,
	float UniformScale,
	FString& OutError)
{
	UGaussianSplatAsset* Asset = CreateSplatAssetFromPly(AbsolutePlyPath, World, OutError);
	if (!Asset)
	{
		return nullptr;
	}
	AActor* Actor = SpawnActorWithAsset(World, Asset, UniformScale, WorldTransform, /*bReuseWorldLabsSingleton*/ false, OutError);
	if (Actor)
	{
		UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: spawned Gaussian splat from %s"), *AbsolutePlyPath);
	}
	return Actor;
}

AActor* FMLSGaussianSplatInterop::SpawnOrReloadWorldLabsSplat(
	UWorld* World,
	const FString& AbsolutePlyPath,
	float UniformScale,
	const FVector& SpawnLocation,
	FString& OutError)
{
	UGaussianSplatAsset* Asset = CreateSplatAssetFromPly(AbsolutePlyPath, World, OutError);
	if (!Asset)
	{
		return nullptr;
	}
	const FTransform Xform(FRotator::ZeroRotator, SpawnLocation, FVector::OneVector);
	AActor* Actor = SpawnActorWithAsset(World, Asset, UniformScale, Xform, /*bReuseWorldLabsSingleton*/ true, OutError);
	if (Actor)
	{
		UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: spawned/reloaded WorldLabs_Splat at %s"), *SpawnLocation.ToString());
	}
	return Actor;
}

bool FMLSGaussianSplatInterop::WorldHasMLSGaussianActor(UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AGaussianSplatActor> It(World); It; ++It)
	{
		return true;
	}
	return false;
}
