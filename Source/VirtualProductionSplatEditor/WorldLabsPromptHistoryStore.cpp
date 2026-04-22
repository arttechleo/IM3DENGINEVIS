#include "WorldLabsPromptHistoryStore.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString FWorldLabsPromptHistoryStore::GetHistoryFilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WorldLabsPromptHistory.txt"));
}

static bool LoadHistoryArray(TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	OutArray.Reset();
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FWorldLabsPromptHistoryStore::GetHistoryFilePath()))
	{
		return true;
	}

	TSharedPtr<FJsonValue> RootVal;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, RootVal) || !RootVal.IsValid() || RootVal->Type != EJson::Array)
	{
		return false;
	}

	OutArray = RootVal->AsArray();
	return true;
}

static bool SaveHistoryArray(const TArray<TSharedPtr<FJsonValue>>& InArray)
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(InArray, Writer))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(Out, *FWorldLabsPromptHistoryStore::GetHistoryFilePath());
}

void FWorldLabsPromptHistoryStore::AppendSubmissionRecord(
	const FString& Environment,
	const FString& TimeOfDay,
	const FString& Mood,
	const FString& Model,
	const FString& Notes)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!LoadHistoryArray(Arr))
	{
		Arr.Reset();
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
	Obj->SetStringField(TEXT("environment"), Environment);
	Obj->SetStringField(TEXT("time_of_day"), TimeOfDay);
	Obj->SetStringField(TEXT("mood"), Mood);
	Obj->SetStringField(TEXT("model"), Model);
	Obj->SetStringField(TEXT("notes"), Notes);
	Obj->SetStringField(TEXT("world_id"), TEXT(""));
	Obj->SetStringField(TEXT("world_url"), TEXT(""));

	Arr.Insert(MakeShared<FJsonValueObject>(Obj), 0);

	const int32 MaxEntries = 40;
	while (Arr.Num() > MaxEntries)
	{
		Arr.RemoveAt(Arr.Num() - 1);
	}

	SaveHistoryArray(Arr);
}

void FWorldLabsPromptHistoryStore::UpdateLatestWorldFields(const FString& WorldId, const FString& WorldUrl)
{
	if (WorldId.IsEmpty())
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!LoadHistoryArray(Arr) || Arr.Num() == 0)
	{
		return;
	}

	TSharedPtr<FJsonValue> First = Arr[0];
	if (!First.IsValid() || First->Type != EJson::Object)
	{
		return;
	}

	TSharedPtr<FJsonObject> Obj = First->AsObject();
	if (!Obj.IsValid())
	{
		return;
	}

	Obj->SetStringField(TEXT("world_id"), WorldId);
	Obj->SetStringField(TEXT("world_url"), WorldUrl);

	SaveHistoryArray(Arr);
}
