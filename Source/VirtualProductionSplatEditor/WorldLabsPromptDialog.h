// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/SlateEnums.h"
#include "Widgets/Views/ITableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "WorldLabsPromptBuilder.h"

class SWindow;
class SMultiLineEditableTextBox;
class SEditableTextBox;
template <typename OptionType>
class SComboBox;
template <typename ItemType>
class SListView;

struct FWorldLabsPromptHistoryEntry
{
	FString Timestamp;
	FString Environment;
	FString TimeOfDay;
	FString Mood;
	FString Model;
	FString Notes;
	FString WorldId;
	FString WorldUrl;
};

class SWorldLabsPromptDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorldLabsPromptDialog) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FString GetPrompt() const { return EnteredPrompt; }
	FString GetModel() const { return SelectedModel; }
	bool WasSubmitted() const { return bSubmitted; }

private:
	static constexpr int32 MaxHistoryEntries = 40;

	TSharedPtr<SWindow> ParentWindow;

	TSharedPtr<SMultiLineEditableTextBox> PreviewTextBox;
	TSharedPtr<SEditableTextBox> NotesTextBox;
	TSharedPtr<SListView<TSharedPtr<FWorldLabsPromptHistoryEntry>>> HistoryListView;

	TArray<TSharedPtr<FWorldLabsPromptHistoryEntry>> PromptHistory;
	TArray<TSharedPtr<FString>> EnvironmentOptions;
	TArray<TSharedPtr<FString>> TimeOfDayOptions;
	TArray<TSharedPtr<FString>> MoodOptions;
	TArray<TSharedPtr<FString>> ModelOptions;

	TSharedPtr<FString> SelectedEnvironmentItem;
	TSharedPtr<FString> SelectedTimeOfDayItem;
	TSharedPtr<FString> SelectedMoodItem;
	TSharedPtr<FString> SelectedModelItem;

	FString EnteredPrompt;
	FString SelectedEnvironment;
	FString SelectedTimeOfDay;
	FString SelectedMood;
	FString SelectedModel = TEXT("Marble 0.1-mini");
	FString AdditionalNotes;
	bool bSubmitted = false;
	FWorldLabsSceneAnalysis SceneAnalysis;
	FString AnalysisWarning;

	void LoadPromptHistory();
	void SavePromptToHistory();
	void UpdatePromptPreview();
	void AnalyzeSceneAndRefresh();
	void RestoreSelectionsFromHistory(const FWorldLabsPromptHistoryEntry& Entry);
	void SetSelectedComboItem(const FString& Value, TArray<TSharedPtr<FString>>& Options, TSharedPtr<FString>& SelectedItem);

	FReply OnSubmitClicked();
	FReply OnCancelClicked();
	FReply OnAnalyzeClicked();
	void OnHistoryItemSelected(TSharedPtr<FWorldLabsPromptHistoryEntry> Item, ESelectInfo::Type SelectType);
	void OnNotesChanged(const FText& InText);

	TSharedRef<ITableRow> OnGenerateHistoryRow(TSharedPtr<FWorldLabsPromptHistoryEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TSharedRef<SWidget> MakeStringComboWidget(TSharedPtr<FString> InItem) const;
	void OnEnvironmentChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType);
	void OnTimeOfDayChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType);
	void OnMoodChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType);
	void OnModelChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType);

	FText GetWarningText() const;
	EVisibility GetWarningVisibility() const;
	FText GetEnvironmentLabel() const;
	FText GetTimeOfDayLabel() const;
	FText GetMoodLabel() const;
	FText GetModelLabel() const;
};
