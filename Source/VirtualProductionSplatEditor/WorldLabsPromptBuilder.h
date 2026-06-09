#pragma once

#include "CoreMinimal.h"
#include "VPSceneAnalysisResult.h"

class UWorld;

class FWorldLabsPromptBuilder
{
public:
	static FSceneAnalysisResult AnalyzeScene(UWorld* World);

	static FString BuildPrompt(
		const FString& Environment,
		const FString& TimeOfDay,
		const FString& Mood,
		const FString& AdditionalNotes,
		const FSceneAnalysisResult& SceneAnalysis);
};
