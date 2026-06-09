#pragma once

#include "CoreMinimal.h"

struct FSceneAnalysisResult
{
	int32  GreyboxActorCount = 0;
	bool   bHasSkyActors     = false;

	FVector SceneBoundsMin;
	FVector SceneBoundsMax;
	FVector SceneExtent;
	FVector SceneCenter;
	float   DominantHorizontalExtentM  = 0.f;
	float   VerticalExtentM            = 0.f;
	float   EstimatedFloorZ            = 0.f;
	int32   WallActorCount             = 0;
	int32   FloorActorCount            = 0;
	int32   PropActorCount             = 0;
	bool    bHasCeiling                = false;
	bool    bIsInteriorSpace           = false;
	bool    bHasOpenHorizon            = false;
	FString SpatialDescription;
	FString DominantShape;
};

// Backward-compat alias — existing editor code that uses FWorldLabsSceneAnalysis keeps compiling.
using FWorldLabsSceneAnalysis = FSceneAnalysisResult;
