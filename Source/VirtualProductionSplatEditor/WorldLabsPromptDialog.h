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
class UClaudePromptRefiner;
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
	virtual ~SWorldLabsPromptDialog();

	// Use UserEditedPrompt if user has manually typed; otherwise use auto-generated EnteredPrompt.
	FString GetPrompt() const { return UserEditedPrompt.IsEmpty() ? EnteredPrompt : UserEditedPrompt; }
	FString GetModel() const { return SelectedModel; }
	bool WasSubmitted() const { return bSubmitted; }
	const TArray<FString>& GetReferenceImagePaths() const { return ReferenceImagePaths; }

private:
	static constexpr int32 MaxHistoryEntries = 40;
	static constexpr int32 MaxRefImages = 3;

	TSharedPtr<SWindow> ParentWindow;

	TSharedPtr<SMultiLineEditableTextBox> PreviewTextBox;
	TSharedPtr<SMultiLineEditableTextBox> RefinedPromptTextBox;
	TSharedPtr<SEditableTextBox> NotesTextBox;
	TSharedPtr<SListView<TSharedPtr<FWorldLabsPromptHistoryEntry>>> HistoryListView;
	TSharedPtr<SListView<TSharedPtr<FString>>> RefImagesListView;

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
	FString UserEditedPrompt;           // Task 2: non-empty when user manually edits the preview box
	FString LastRefinedPrompt;
	FString SelectedEnvironment;
	FString SelectedTimeOfDay;
	FString SelectedMood;
	FString SelectedModel = TEXT("Marble 0.1-mini");
	FString AdditionalNotes;
	bool bSubmitted = false;
	FWorldLabsSceneAnalysis SceneAnalysis;
	FString AnalysisWarning;

	// Task 3: Reference image paths
	TArray<FString> ReferenceImagePaths;
	TArray<TSharedPtr<FString>> RefImagePathItems;

	// Task 6: Analyze Scene async state
	UClaudePromptRefiner* AnalyzeRefiner = nullptr; // AddToRoot'd while HTTP request is in flight
	FString AnalyzeScreenshotPath;
	double AnalyzeTickStart = 0.0;
	FDelegateHandle AnalyzeTickerHandle;

	void LoadPromptHistory();
	FReply OnCopyRefinedPromptClicked();
	EVisibility GetRefinedPromptVisibility() const;
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

	// Task 3: Reference image list
	FReply OnAddRefImageClicked();
	void OnRemoveRefImage(TSharedPtr<FString> Item);
	TSharedRef<ITableRow> OnGenerateRefImageRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	EVisibility GetRefImagesListVisibility() const;

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

	// Task 6: Analyze Scene async helpers
	bool PollForAnalyzeScreenshot(float DeltaTime);
	void DoAnalyzeWithScreenshot(const FString& ScreenshotPath);
	void OnAnalyzeRefineComplete(FString Refined);
};
