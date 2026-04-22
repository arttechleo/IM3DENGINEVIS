#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FWorldLabsSceneAnalysis
{
	int32 GreyboxActorCount = 0;
	bool bHasSkyActors = false;
};

class FWorldLabsPromptBuilder
{
public:
	static FWorldLabsSceneAnalysis AnalyzeScene(UWorld* World);

	static FString BuildPrompt(
		const FString& Environment,
		const FString& TimeOfDay,
		const FString& Mood,
		const FString& AdditionalNotes,
		const FWorldLabsSceneAnalysis& SceneAnalysis);
};
