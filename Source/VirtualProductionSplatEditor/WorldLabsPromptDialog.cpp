// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldLabsPromptDialog.h"
#include "WorldLabsPromptHistoryStore.h"
#include "Editor.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"

namespace WorldLabsPromptDialogInternal
{
	static FString ToDisplayDate(const FString& IsoTimestamp)
	{
		return IsoTimestamp.Len() >= 10 ? IsoTimestamp.Left(10) : IsoTimestamp;
	}
}

void SWorldLabsPromptDialog::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;

	EnvironmentOptions = {
		MakeShared<FString>(TEXT("Desert Ruins")),
		MakeShared<FString>(TEXT("Lush Forest")),
		MakeShared<FString>(TEXT("Modern City District")),
		MakeShared<FString>(TEXT("Coastal Cliffs")),
		MakeShared<FString>(TEXT("Mountain Valley")),
		MakeShared<FString>(TEXT("Mediterranean Village")),
		MakeShared<FString>(TEXT("Industrial Yard")),
		MakeShared<FString>(TEXT("Sci-Fi Megastructure"))
	};
	TimeOfDayOptions = {
		MakeShared<FString>(TEXT("Morning")),
		MakeShared<FString>(TEXT("Midday")),
		MakeShared<FString>(TEXT("Golden Hour")),
		MakeShared<FString>(TEXT("Blue Hour")),
		MakeShared<FString>(TEXT("Night"))
	};
	MoodOptions = {
		MakeShared<FString>(TEXT("Cinematic")),
		MakeShared<FString>(TEXT("Documentary")),
		MakeShared<FString>(TEXT("Dramatic")),
		MakeShared<FString>(TEXT("Peaceful")),
		MakeShared<FString>(TEXT("Mysterious"))
	};
	ModelOptions = {
		MakeShared<FString>(TEXT("Marble 0.1-mini")),
		MakeShared<FString>(TEXT("Marble 0.1-plus"))
	};

	SelectedEnvironmentItem = EnvironmentOptions[0];
	SelectedTimeOfDayItem = TimeOfDayOptions[2];
	SelectedMoodItem = MoodOptions[0];
	SelectedModelItem = ModelOptions[0];
	SelectedEnvironment = *SelectedEnvironmentItem;
	SelectedTimeOfDay = *SelectedTimeOfDayItem;
	SelectedMood = *SelectedMoodItem;
	SelectedModel = *SelectedModelItem;

	LoadPromptHistory();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("WorldLabsPromptDialog", "Title", "Submit to WorldLabs — Enter Generation Prompt"))
			.Font(FAppStyle::GetFontStyle("NormalFontBold"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(STextBlock)
			.Text(this, &SWorldLabsPromptDialog::GetWarningText)
			.Visibility(this, &SWorldLabsPromptDialog::GetWarningVisibility)
			.ColorAndOpacity(FLinearColor(1.0f, 0.35f, 0.35f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "EnvironmentLabel", "Environment:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&EnvironmentOptions)
				.OnGenerateWidget(this, &SWorldLabsPromptDialog::MakeStringComboWidget)
				.OnSelectionChanged(this, &SWorldLabsPromptDialog::OnEnvironmentChanged)
				.InitiallySelectedItem(SelectedEnvironmentItem)
				.Content()
				[
					SNew(STextBlock).Text(this, &SWorldLabsPromptDialog::GetEnvironmentLabel)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "TimeLabel", "Time of Day:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&TimeOfDayOptions)
				.OnGenerateWidget(this, &SWorldLabsPromptDialog::MakeStringComboWidget)
				.OnSelectionChanged(this, &SWorldLabsPromptDialog::OnTimeOfDayChanged)
				.InitiallySelectedItem(SelectedTimeOfDayItem)
				.Content()
				[
					SNew(STextBlock).Text(this, &SWorldLabsPromptDialog::GetTimeOfDayLabel)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "MoodLabel", "Mood:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&MoodOptions)
				.OnGenerateWidget(this, &SWorldLabsPromptDialog::MakeStringComboWidget)
				.OnSelectionChanged(this, &SWorldLabsPromptDialog::OnMoodChanged)
				.InitiallySelectedItem(SelectedMoodItem)
				.Content()
				[
					SNew(STextBlock).Text(this, &SWorldLabsPromptDialog::GetMoodLabel)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "ModelLabel", "Model:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ModelOptions)
				.OnGenerateWidget(this, &SWorldLabsPromptDialog::MakeStringComboWidget)
				.OnSelectionChanged(this, &SWorldLabsPromptDialog::OnModelChanged)
				.InitiallySelectedItem(SelectedModelItem)
				.Content()
				[
					SNew(STextBlock).Text(this, &SWorldLabsPromptDialog::GetModelLabel)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "NotesLabel", "Additional Notes:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(NotesTextBox, SEditableTextBox)
				.OnTextChanged(this, &SWorldLabsPromptDialog::OnNotesChanged)
			]
		]
		+ SVerticalBox::Slot().FillHeight(0.38f).Padding(8.f, 4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "PreviewLabel", "Generated Prompt Preview (read-only):"))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(PreviewTextBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(true)
			]
		]
		+ SVerticalBox::Slot().FillHeight(0.28f).Padding(8.f, 4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "HistoryLabel", "Prompt History:"))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(HistoryListView, SListView<TSharedPtr<FWorldLabsPromptHistoryEntry>>)
				.ListItemsSource(&PromptHistory)
				.OnGenerateRow(this, &SWorldLabsPromptDialog::OnGenerateHistoryRow)
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged(this, &SWorldLabsPromptDialog::OnHistoryItemSelected)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FMargin(4.f, 0.f))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("WorldLabsPromptDialog", "AnalyzeScene", "Analyze Scene"))
				.OnClicked(this, &SWorldLabsPromptDialog::OnAnalyzeClicked)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("WorldLabsPromptDialog", "Submit", "Submit"))
				.OnClicked(this, &SWorldLabsPromptDialog::OnSubmitClicked)
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				SNew(SButton)
				.Text(NSLOCTEXT("WorldLabsPromptDialog", "Cancel", "Cancel"))
				.OnClicked(this, &SWorldLabsPromptDialog::OnCancelClicked)
			]
		]
	];

	AnalyzeSceneAndRefresh();
	if (HistoryListView.IsValid())
	{
		HistoryListView->RequestListRefresh();
	}
}

void SWorldLabsPromptDialog::LoadPromptHistory()
{
	PromptHistory.Reset();

	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FWorldLabsPromptHistoryStore::GetHistoryFilePath()))
	{
		return;
	}

	TSharedPtr<FJsonValue> RootVal;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, RootVal) || !RootVal.IsValid() || RootVal->Type != EJson::Array)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Val : RootVal->AsArray())
	{
		if (!Val.IsValid() || Val->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Obj = Val->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}

		TSharedPtr<FWorldLabsPromptHistoryEntry> Entry = MakeShared<FWorldLabsPromptHistoryEntry>();
		Obj->TryGetStringField(TEXT("timestamp"), Entry->Timestamp);
		Obj->TryGetStringField(TEXT("environment"), Entry->Environment);
		Obj->TryGetStringField(TEXT("time_of_day"), Entry->TimeOfDay);
		Obj->TryGetStringField(TEXT("mood"), Entry->Mood);
		Obj->TryGetStringField(TEXT("model"), Entry->Model);
		Obj->TryGetStringField(TEXT("notes"), Entry->Notes);
		Obj->TryGetStringField(TEXT("world_id"), Entry->WorldId);
		Obj->TryGetStringField(TEXT("world_url"), Entry->WorldUrl);
		PromptHistory.Add(Entry);
	}
}

void SWorldLabsPromptDialog::SavePromptToHistory()
{
	FWorldLabsPromptHistoryStore::AppendSubmissionRecord(
		SelectedEnvironment,
		SelectedTimeOfDay,
		SelectedMood,
		SelectedModel,
		AdditionalNotes);

	LoadPromptHistory();
	if (HistoryListView.IsValid())
	{
		HistoryListView->RequestListRefresh();
	}
}

void SWorldLabsPromptDialog::UpdatePromptPreview()
{
	EnteredPrompt = FWorldLabsPromptBuilder::BuildPrompt(
		SelectedEnvironment,
		SelectedTimeOfDay,
		SelectedMood,
		AdditionalNotes,
		SceneAnalysis);

	if (PreviewTextBox.IsValid())
	{
		PreviewTextBox->SetText(FText::FromString(EnteredPrompt));
	}
}

void SWorldLabsPromptDialog::AnalyzeSceneAndRefresh()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	SceneAnalysis = FWorldLabsPromptBuilder::AnalyzeScene(World);
	AnalysisWarning = SceneAnalysis.GreyboxActorCount == 0
		? TEXT("No greybox geometry found. Run Setup VP Level first.")
		: FString();
	UpdatePromptPreview();
}

void SWorldLabsPromptDialog::RestoreSelectionsFromHistory(const FWorldLabsPromptHistoryEntry& Entry)
{
	SetSelectedComboItem(Entry.Environment, EnvironmentOptions, SelectedEnvironmentItem);
	SetSelectedComboItem(Entry.TimeOfDay, TimeOfDayOptions, SelectedTimeOfDayItem);
	SetSelectedComboItem(Entry.Mood, MoodOptions, SelectedMoodItem);
	SetSelectedComboItem(Entry.Model, ModelOptions, SelectedModelItem);

	SelectedEnvironment = SelectedEnvironmentItem.IsValid() ? *SelectedEnvironmentItem : SelectedEnvironment;
	SelectedTimeOfDay = SelectedTimeOfDayItem.IsValid() ? *SelectedTimeOfDayItem : SelectedTimeOfDay;
	SelectedMood = SelectedMoodItem.IsValid() ? *SelectedMoodItem : SelectedMood;
	SelectedModel = SelectedModelItem.IsValid() ? *SelectedModelItem : SelectedModel;
	AdditionalNotes = Entry.Notes;

	if (NotesTextBox.IsValid())
	{
		NotesTextBox->SetText(FText::FromString(AdditionalNotes));
	}

	UpdatePromptPreview();
}

void SWorldLabsPromptDialog::SetSelectedComboItem(const FString& Value, TArray<TSharedPtr<FString>>& Options, TSharedPtr<FString>& SelectedItem)
{
	for (const TSharedPtr<FString>& Option : Options)
	{
		if (Option.IsValid() && *Option == Value)
		{
			SelectedItem = Option;
			return;
		}
	}
}

TSharedRef<SWidget> SWorldLabsPromptDialog::MakeStringComboWidget(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(InItem.IsValid() ? FText::FromString(*InItem) : FText::GetEmpty());
}

void SWorldLabsPromptDialog::OnEnvironmentChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType)
{
	if (NewSelection.IsValid())
	{
		SelectedEnvironmentItem = NewSelection;
		SelectedEnvironment = *NewSelection;
		UpdatePromptPreview();
	}
}

void SWorldLabsPromptDialog::OnTimeOfDayChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType)
{
	if (NewSelection.IsValid())
	{
		SelectedTimeOfDayItem = NewSelection;
		SelectedTimeOfDay = *NewSelection;
		UpdatePromptPreview();
	}
}

void SWorldLabsPromptDialog::OnMoodChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType)
{
	if (NewSelection.IsValid())
	{
		SelectedMoodItem = NewSelection;
		SelectedMood = *NewSelection;
		UpdatePromptPreview();
	}
}

void SWorldLabsPromptDialog::OnModelChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectType)
{
	if (NewSelection.IsValid())
	{
		SelectedModelItem = NewSelection;
		SelectedModel = *NewSelection;
	}
}

void SWorldLabsPromptDialog::OnNotesChanged(const FText& InText)
{
	AdditionalNotes = InText.ToString();
	UpdatePromptPreview();
}

TSharedRef<ITableRow> SWorldLabsPromptDialog::OnGenerateHistoryRow(
	TSharedPtr<FWorldLabsPromptHistoryEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Display;
	if (Item.IsValid())
	{
		Display = FString::Printf(
			TEXT("[%s] %s (%s) — %s"),
			*Item->TimeOfDay,
			*Item->Environment,
			*Item->Mood,
			*WorldLabsPromptDialogInternal::ToDisplayDate(Item->Timestamp));
	}

	return SNew(STableRow<TSharedPtr<FWorldLabsPromptHistoryEntry>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Display))
		.ToolTipText(Item.IsValid() ? FText::FromString(Item->Notes) : FText::GetEmpty())
	];
}

void SWorldLabsPromptDialog::OnHistoryItemSelected(TSharedPtr<FWorldLabsPromptHistoryEntry> Item, ESelectInfo::Type SelectType)
{
	if (Item.IsValid())
	{
		RestoreSelectionsFromHistory(*Item);
	}
}

FReply SWorldLabsPromptDialog::OnAnalyzeClicked()
{
	AnalyzeSceneAndRefresh();
	return FReply::Handled();
}

FReply SWorldLabsPromptDialog::OnSubmitClicked()
{
	SavePromptToHistory();
	bSubmitted = true;
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().RequestDestroyWindow(ParentWindow.ToSharedRef());
	}
	return FReply::Handled();
}

FReply SWorldLabsPromptDialog::OnCancelClicked()
{
	bSubmitted = false;
	EnteredPrompt.Reset();
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().RequestDestroyWindow(ParentWindow.ToSharedRef());
	}
	return FReply::Handled();
}

FText SWorldLabsPromptDialog::GetWarningText() const
{
	return FText::FromString(AnalysisWarning);
}

EVisibility SWorldLabsPromptDialog::GetWarningVisibility() const
{
	return AnalysisWarning.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SWorldLabsPromptDialog::GetEnvironmentLabel() const
{
	return SelectedEnvironmentItem.IsValid() ? FText::FromString(*SelectedEnvironmentItem) : FText::FromString(SelectedEnvironment);
}

FText SWorldLabsPromptDialog::GetTimeOfDayLabel() const
{
	return SelectedTimeOfDayItem.IsValid() ? FText::FromString(*SelectedTimeOfDayItem) : FText::FromString(SelectedTimeOfDay);
}

FText SWorldLabsPromptDialog::GetMoodLabel() const
{
	return SelectedMoodItem.IsValid() ? FText::FromString(*SelectedMoodItem) : FText::FromString(SelectedMood);
}

FText SWorldLabsPromptDialog::GetModelLabel() const
{
	return SelectedModelItem.IsValid() ? FText::FromString(*SelectedModelItem) : FText::FromString(SelectedModel);
}
