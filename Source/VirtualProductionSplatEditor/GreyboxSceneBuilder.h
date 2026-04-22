// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityActor.h"
#include "GreyboxSceneBuilder.generated.h"

/**
 * Editor-only utility: spawns a greybox layout, lighting, sky, and a hero CineCamera into the current editor level.
 */
UCLASS(Blueprintable, BlueprintType, placeable)
class VIRTUALPRODUCTIONSPLATEDITOR_API AGreyboxSceneBuilder : public AEditorUtilityActor
{
	GENERATED_BODY()

public:
	/** Spawns / replaces greybox content (tags previous GreyboxVPSplatBuild actors for removal). */
	UFUNCTION(CallInEditor, Category = "VP Pipeline")
	void BuildGreyboxScene();
};
