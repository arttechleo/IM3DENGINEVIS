#pragma once

#include "CoreMinimal.h"

struct FWorldLabsPromptHistoryStore
{
	static FString GetHistoryFilePath();

	static void AppendSubmissionRecord(
		const FString& Environment,
		const FString& TimeOfDay,
		const FString& Mood,
		const FString& Model,
		const FString& Notes);

	static void UpdateLatestWorldFields(const FString& WorldId, const FString& WorldUrl);

	static FString GetLastRefinedPromptFilePath();
	static void SaveLastRefinedPrompt(const FString& Prompt);
	static FString LoadLastRefinedPrompt();
};
