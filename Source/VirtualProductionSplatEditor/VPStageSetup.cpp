// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPStageSetup.h"
#include "CineCameraActor.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
AVPStageSetup::AVPStageSetup()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AVPStageSetup::Step1_FindCamera()
{
	CachedVpCamera = nullptr;

	if (VpCameraOverride)
	{
		CachedVpCamera = VpCameraOverride;
		UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step1: using VpCameraOverride — %s"), *CachedVpCamera->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("VPStageSetup Step1: no world."));
		return;
	}

	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Contains(TEXT("PrimaryCamera")))
		{
			CachedVpCamera = *It;
			UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step1: found PrimaryCamera — %s"), *CachedVpCamera->GetName());
			return;
		}
	}

	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		CachedVpCamera = *It;
		UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step1: using first CineCameraActor — %s"), *CachedVpCamera->GetName());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("VPStageSetup Step1: no CineCameraActor in level."));
}

void AVPStageSetup::Step2_AddFillLights()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Base = GaussianSplatActor ? GaussianSplatActor->GetActorLocation() : FVector::ZeroVector;

	for (AActor* Old : SpawnedFillActors)
	{
		if (Old)
		{
			Old->Destroy();
		}
	}
	SpawnedFillActors.Reset();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	auto SpawnFill = [&](const FVector& Loc, const FRotator& Rot, const FString& Label)
	{
		if (ADirectionalLight* L = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(Rot, Loc), Params))
		{
			L->SetActorLabel(Label);
			if (ULightComponent* LC = L->GetLightComponent())
			{
				LC->SetIntensity(2.f);
				LC->SetLightColor(FLinearColor(0.95f, 0.98f, 1.f));
				LC->SetMobility(EComponentMobility::Movable);
			}
			SpawnedFillActors.Add(L);
		}
	};

	SpawnFill(Base + FVector(0.f, 800.f, 400.f), FRotator(-35.f, -120.f, 0.f), TEXT("VP_FillKey"));
	SpawnFill(Base + FVector(-600.f, -400.f, 200.f), FRotator(-25.f, 40.f, 0.f), TEXT("VP_FillRim"));

	UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step2: added %d fill lights near %s"), SpawnedFillActors.Num(), *Base.ToString());
}

void AVPStageSetup::Step3_AddPostProcess()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (PostProcessVolume)
	{
		PostProcessVolume->Destroy();
		PostProcessVolume = nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Loc = GaussianSplatActor ? GaussianSplatActor->GetActorLocation() : FVector::ZeroVector;

	PostProcessVolume = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform(Loc), Params);
	if (PostProcessVolume)
	{
		PostProcessVolume->SetActorLabel(TEXT("VP_PostProcess"));
		PostProcessVolume->bUnbound = true;
		PostProcessVolume->Settings.bOverride_ColorGradingIntensity = true;
		PostProcessVolume->Settings.ColorGradingIntensity = 1.f;
		UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step3: post process volume spawned."));
	}
}

void AVPStageSetup::Step4_LogStageSummary()
{
	UE_LOG(LogTemp, Log, TEXT("VPStageSetup Step4: --- Stage summary ---"));
	UE_LOG(LogTemp, Log, TEXT("  GaussianSplatActor: %s"),
		GaussianSplatActor ? *GaussianSplatActor->GetName() : TEXT("null"));
	UE_LOG(LogTemp, Log, TEXT("  VP Camera: %s"),
		CachedVpCamera ? *CachedVpCamera->GetName() : TEXT("null"));
	UE_LOG(LogTemp, Log, TEXT("  Fill lights spawned: %d"), SpawnedFillActors.Num());
	UE_LOG(LogTemp, Log, TEXT("  Post process: %s"),
		PostProcessVolume ? *PostProcessVolume->GetName() : TEXT("null"));
}
