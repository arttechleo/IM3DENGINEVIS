// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VPPipelineOrchestrator.generated.h"

class AGreyboxSceneBuilder;
class APanoramicCapture360;
class APanoramicExportRunner;
class AWorldLabsRunner;
class AGaussianSplatImportRunner;
class AVPStageSetup;

UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API AVPPipelineOrchestrator : public AActor
{
	GENERATED_BODY()

public:
	AVPPipelineOrchestrator();

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<AGreyboxSceneBuilder> SceneBuilder;

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<APanoramicCapture360> CameraRig;

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<APanoramicExportRunner> ExportRunner;

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<AWorldLabsRunner> WorldLabsRunner;

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<AGaussianSplatImportRunner> SplatImporter;

	UPROPERTY(EditAnywhere, Category = "Pipeline|References")
	TObjectPtr<AVPStageSetup> StageSetup;

	UFUNCTION(CallInEditor, Category = "Pipeline")
	void RunFullPipeline();

	UFUNCTION(CallInEditor, Category = "Pipeline")
	void LogPipelineStatus();

private:
	bool LevelHasGaussianSplatActor() const;
};
