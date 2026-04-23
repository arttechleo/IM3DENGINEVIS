// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldLabsPromptDialog.h"
#include "ClaudePromptRefiner.h"
#include "WorldLabsPromptHistoryStore.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Layout/SExpandableArea.h"
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
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"

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
	LastRefinedPrompt = FWorldLabsPromptHistoryStore::LoadLastRefinedPrompt();

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
		// Task 2: editable prompt preview
		+ SVerticalBox::Slot().FillHeight(0.32f).Padding(8.f, 4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(NSLOCTEXT("WorldLabsPromptDialog", "PreviewLabel", "Generated Prompt Preview (editable — overrides auto-generated):"))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(PreviewTextBox, SMultiLineEditableTextBox)
				.IsReadOnly(false)
				.AutoWrapText(true)
				.OnTextChanged_Lambda([this](const FText& T)
				{
					// Only capture user edits (SetText doesn't fire this)
					UserEditedPrompt = T.ToString();
				})
			]
		]
		// Task 3: style reference images
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 2.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("WorldLabsPromptDialog", "RefImagesLabel", "Style Reference Images (optional, max 3):"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("WorldLabsPromptDialog", "AddRefImage", "+ Add Image"))
					.OnClicked(this, &SWorldLabsPromptDialog::OnAddRefImageClicked)
					.IsEnabled_Lambda([this]() -> bool { return ReferenceImagePaths.Num() < MaxRefImages; })
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(72.f)
				.Visibility(this, &SWorldLabsPromptDialog::GetRefImagesListVisibility)
				[
					SAssignNew(RefImagesListView, SListView<TSharedPtr<FString>>)
					.ListItemsSource(&RefImagePathItems)
					.OnGenerateRow(this, &SWorldLabsPromptDialog::OnGenerateRefImageRow)
					.SelectionMode(ESelectionMode::None)
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(0.22f).Padding(8.f, 4.f)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f)
		[
			SNew(SExpandableArea)
			.AreaTitle(NSLOCTEXT("WorldLabsPromptDialog", "RefinedPromptExpand", "AI-Refined Prompt (sent to WorldLabs)"))
			.InitiallyCollapsed(false)
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SAssignNew(RefinedPromptTextBox, SMultiLineEditableTextBox)
						.IsReadOnly(true)
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(NSLOCTEXT("WorldLabsPromptDialog", "CopyRefined", "Copy"))
						.OnClicked(this, &SWorldLabsPromptDialog::OnCopyRefinedPromptClicked)
						.Visibility(this, &SWorldLabsPromptDialog::GetRefinedPromptVisibility)
					]
				]
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
	if (RefinedPromptTextBox.IsValid() && !LastRefinedPrompt.IsEmpty())
	{
		RefinedPromptTextBox->SetText(FText::FromString(LastRefinedPrompt));
	}
}

SWorldLabsPromptDialog::~SWorldLabsPromptDialog()
{
	if (AnalyzeTickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(AnalyzeTickerHandle);
		AnalyzeTickerHandle.Reset();
	}
	if (AnalyzeRefiner)
	{
		AnalyzeRefiner->RemoveFromRoot();
		AnalyzeRefiner = nullptr;
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
		// Programmatic SetText does NOT fire OnTextChanged, so UserEditedPrompt stays intact.
		// Only update box if user hasn't manually overridden the prompt.
		if (UserEditedPrompt.IsEmpty())
		{
			PreviewTextBox->SetText(FText::FromString(EnteredPrompt));
		}
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

	UserEditedPrompt.Reset(); // clear any manual override so auto-preview refreshes
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

// ---- Task 3: Reference image helpers ----

FReply SWorldLabsPromptDialog::OnAddRefImageClicked()
{
	if (ReferenceImagePaths.Num() >= MaxRefImages)
	{
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = ParentWindow.IsValid()
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	TArray<FString> OutFiles;
	DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Style Reference Image"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Image Files|*.png;*.jpg;*.jpeg;*.exr"),
		EFileDialogFlags::None,
		OutFiles);

	for (const FString& F : OutFiles)
	{
		if (!ReferenceImagePaths.Contains(F) && ReferenceImagePaths.Num() < MaxRefImages)
		{
			ReferenceImagePaths.Add(F);
			RefImagePathItems.Add(MakeShared<FString>(F));
		}
	}

	if (RefImagesListView.IsValid())
	{
		RefImagesListView->RequestListRefresh();
	}

	return FReply::Handled();
}

void SWorldLabsPromptDialog::OnRemoveRefImage(TSharedPtr<FString> Item)
{
	if (Item.IsValid())
	{
		ReferenceImagePaths.Remove(*Item);
		RefImagePathItems.Remove(Item);
	}
	if (RefImagesListView.IsValid())
	{
		RefImagesListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SWorldLabsPromptDialog::OnGenerateRefImageRow(
	TSharedPtr<FString> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	TWeakPtr<SWorldLabsPromptDialog> WeakThis(StaticCastSharedRef<SWorldLabsPromptDialog>(AsShared()));

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Item.IsValid() ? FText::FromString(FPaths::GetCleanFilename(*Item)) : FText::GetEmpty())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("x")))
			.OnClicked_Lambda([WeakThis, Item]() -> FReply
			{
				if (TSharedPtr<SWorldLabsPromptDialog> Pinned = WeakThis.Pin())
				{
					Pinned->OnRemoveRefImage(Item);
				}
				return FReply::Handled();
			})
		]
	];
}

EVisibility SWorldLabsPromptDialog::GetRefImagesListVisibility() const
{
	return RefImagePathItems.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

// ---- Task 6: Analyze Scene with Claude ----

FReply SWorldLabsPromptDialog::OnAnalyzeClicked()
{
	FString AnthropicKey;
	if (GConfig)
	{
		GConfig->GetString(TEXT("AnthropicAPI"), TEXT("APIKey"), AnthropicKey, GGameIni);
	}

	if (AnthropicKey.IsEmpty() || AnthropicKey == TEXT("YOUR_KEY_HERE"))
	{
		// No key — fall back to local scene analysis only
		AnalyzeSceneAndRefresh();
		return FReply::Handled();
	}

	// Delete any stale capture so the file-presence check works reliably
	AnalyzeScreenshotPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VP_Analyze_capture.png"));
	if (FPaths::FileExists(AnalyzeScreenshotPath))
	{
		IFileManager::Get().Delete(*AnalyzeScreenshotPath, false, true);
	}

	FScreenshotRequest::RequestScreenshot(AnalyzeScreenshotPath, false, false);

	AnalyzeTickStart = FPlatformTime::Seconds();

	if (AnalyzeTickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(AnalyzeTickerHandle);
	}

	TWeakPtr<SWorldLabsPromptDialog> WeakThis(StaticCastSharedRef<SWorldLabsPromptDialog>(AsShared()));
	FString PathCopy = AnalyzeScreenshotPath;
	double StartCopy = AnalyzeTickStart;

	AnalyzeTickerHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis, PathCopy, StartCopy](float) -> bool
		{
			TSharedPtr<SWorldLabsPromptDialog> Pinned = WeakThis.Pin();
			if (!Pinned.IsValid())
			{
				return false;
			}
			return Pinned->PollForAnalyzeScreenshot(0.f);
		}), 0.15f);

	return FReply::Handled();
}

bool SWorldLabsPromptDialog::PollForAnalyzeScreenshot(float /*DeltaTime*/)
{
	if (FPaths::FileExists(AnalyzeScreenshotPath))
	{
		AnalyzeTickerHandle.Reset();
		DoAnalyzeWithScreenshot(AnalyzeScreenshotPath);
		return false;
	}

	// 5s timeout — fall back to local analysis
	if (FPlatformTime::Seconds() - AnalyzeTickStart > 5.0)
	{
		AnalyzeTickerHandle.Reset();
		AnalyzeSceneAndRefresh();
		return false;
	}

	return true;
}

void SWorldLabsPromptDialog::DoAnalyzeWithScreenshot(const FString& ScreenshotPath)
{
	FString AnthropicKey;
	if (GConfig)
	{
		GConfig->GetString(TEXT("AnthropicAPI"), TEXT("APIKey"), AnthropicKey, GGameIni);
	}

	if (AnthropicKey.IsEmpty() || AnthropicKey == TEXT("YOUR_KEY_HERE"))
	{
		AnalyzeSceneAndRefresh();
		return;
	}

	const FString UserIntent = FString::Printf(TEXT("%s environment, %s, %s mood"),
		*SelectedEnvironment, *SelectedTimeOfDay, *SelectedMood);

	if (!AnalyzeRefiner)
	{
		AnalyzeRefiner = NewObject<UClaudePromptRefiner>(GetTransientPackage());
		AnalyzeRefiner->AddToRoot();
	}

	TWeakPtr<SWorldLabsPromptDialog> WeakThis(StaticCastSharedRef<SWorldLabsPromptDialog>(AsShared()));
	UClaudePromptRefiner* RefinerRaw = AnalyzeRefiner;

	TArray<FString> NoRefs;
	AnalyzeRefiner->RefinePrompt(
		AnthropicKey,
		ScreenshotPath,
		UserIntent,
		NoRefs,
		FOnRefinedPrompt::CreateLambda([WeakThis, RefinerRaw](FString Refined)
		{
			// Always clean up root reference
			if (RefinerRaw)
			{
				RefinerRaw->RemoveFromRoot();
			}

			// Only update UI if dialog still alive
			if (TSharedPtr<SWorldLabsPromptDialog> Pinned = WeakThis.Pin())
			{
				Pinned->AnalyzeRefiner = nullptr;
				Pinned->OnAnalyzeRefineComplete(Refined);
			}
		}));
}

void SWorldLabsPromptDialog::OnAnalyzeRefineComplete(FString Refined)
{
	if (Refined.IsEmpty())
	{
		AnalyzeSceneAndRefresh();
		return;
	}

	// Populate the editable prompt with Claude's response, do NOT submit
	UserEditedPrompt = Refined;
	EnteredPrompt = Refined;
	if (PreviewTextBox.IsValid())
	{
		PreviewTextBox->SetText(FText::FromString(Refined));
	}
}

// ---- Submit / Cancel ----

FReply SWorldLabsPromptDialog::OnSubmitClicked()
{
	// Use user-edited prompt if provided, otherwise keep auto-generated EnteredPrompt
	if (!UserEditedPrompt.IsEmpty())
	{
		EnteredPrompt = UserEditedPrompt;
	}
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

FReply SWorldLabsPromptDialog::OnCopyRefinedPromptClicked()
{
	if (!LastRefinedPrompt.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*LastRefinedPrompt);
	}
	return FReply::Handled();
}

EVisibility SWorldLabsPromptDialog::GetRefinedPromptVisibility() const
{
	return LastRefinedPrompt.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}
