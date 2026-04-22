// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPPipelineOrchestrator.h"
#include "GreyboxSceneBuilder.h"
#include "MultiAngleCameraRig.h"
#include "GreyboxExportRunner.h"
#include "WorldLabsRunner.h"
#include "GaussianSplatImportRunner.h"
#include "VPStageSetup.h"
#include "GaussianSplatImportHelper.h"
#include "Engine/World.h"

AVPPipelineOrchestrator::AVPPipelineOrchestrator()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AVPPipelineOrchestrator::LevelHasGaussianSplatActor() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	return UGaussianSplatImportHelper::WorldHasMLSGaussianActorInWorld(World);
}

void AVPPipelineOrchestrator::RunFullPipeline()
{
	if (!SceneBuilder)
	{
		UE_LOG(LogTemp, Error, TEXT("VPPipelineOrchestrator: missing SceneBuilder."));
	}
	if (!CameraRig && !ExportRunner)
	{
		UE_LOG(LogTemp, Error, TEXT("VPPipelineOrchestrator: missing CameraRig and ExportRunner (need one for panorama capture)."));
	}
	if (!WorldLabsRunner)
	{
		UE_LOG(LogTemp, Error, TEXT("VPPipelineOrchestrator: missing WorldLabsRunner."));
	}

	if (!SceneBuilder || !WorldLabsRunner || (!CameraRig && !ExportRunner))
	{
		UE_LOG(LogTemp, Error, TEXT("VPPipelineOrchestrator: RunFullPipeline aborted (critical references missing)."));
		return;
	}

	if (!SplatImporter)
	{
		UE_LOG(LogTemp, Warning, TEXT("VPPipelineOrchestrator: SplatImporter not set (optional for automated submit)."));
	}
	if (!StageSetup)
	{
		UE_LOG(LogTemp, Warning, TEXT("VPPipelineOrchestrator: StageSetup not set (optional)."));
	}

	// 1) Greybox: only if no splat scene yet (user may re-run after splat import)
	if (LevelHasGaussianSplatActor())
	{
		UE_LOG(LogTemp, Log, TEXT("VPPipelineOrchestrator: AGaussianSplattingActor already in level — skipping BuildGreyboxScene."));
	}
	else
	{
		SceneBuilder->BuildGreyboxScene();
	}

	if (ExportRunner)
	{
		ExportRunner->CapturePanorama();
	}
	else if (CameraRig)
	{
		CameraRig->CaptureFaces();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VPPipelineOrchestrator: missing ExportRunner and CameraRig — cannot capture panorama."));
	}
	WorldLabsRunner->SubmitToWorldLabs();

	const FString OpId = WorldLabsRunner->CurrentOperationID;
	UE_LOG(LogTemp, Log, TEXT("WorldLabs submit started (upload + generate). Operation ID when ready: %s. Poll with CheckJobStatus(). "
							  "Once complete, call DownloadSplat() then use SplatImporter to place it, "
							  "then run StageSetup steps 1-4."),
		OpId.IsEmpty() ? TEXT("(pending async — uploads/generation in progress)") : *OpId);
}

void AVPPipelineOrchestrator::LogPipelineStatus()
{
	auto LogRef = [](const TCHAR* Name, const AActor* A)
	{
		UE_LOG(LogTemp, Log, TEXT("  %s: %s"), Name, A ? *A->GetName() : TEXT("null"));
	};

	UE_LOG(LogTemp, Log, TEXT("VPPipelineOrchestrator — pipeline status"));
	LogRef(TEXT("SceneBuilder"), SceneBuilder);
	LogRef(TEXT("CameraRig"), CameraRig);
	LogRef(TEXT("ExportRunner"), ExportRunner);
	LogRef(TEXT("WorldLabsRunner"), WorldLabsRunner);
	LogRef(TEXT("SplatImporter"), SplatImporter);
	LogRef(TEXT("StageSetup"), StageSetup);

	if (WorldLabsRunner)
	{
		UE_LOG(LogTemp, Log, TEXT("  WorldLabs CurrentOperationID: %s"), *WorldLabsRunner->CurrentOperationID);
		UE_LOG(LogTemp, Log, TEXT("  WorldLabs CurrentStatus: %s"), *WorldLabsRunner->CurrentStatus);
		UE_LOG(LogTemp, Log, TEXT("  WorldLabs CurrentDownloadURL: %s"), *WorldLabsRunner->CurrentDownloadURL);
	}

	if (SplatImporter && SplatImporter->LastSpawnedSplat)
	{
		UE_LOG(LogTemp, Log, TEXT("  LastSpawnedSplat: %s"), *SplatImporter->LastSpawnedSplat->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("  LastSpawnedSplat: null"));
	}
}
