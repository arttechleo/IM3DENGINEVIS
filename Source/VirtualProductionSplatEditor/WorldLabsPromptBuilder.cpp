#include "WorldLabsPromptBuilder.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"

FWorldLabsSceneAnalysis FWorldLabsPromptBuilder::AnalyzeScene(UWorld* World)
{
	FWorldLabsSceneAnalysis Result;
	if (!World)
	{
		return Result;
	}

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		const bool bTaggedGreybox = Actor->ActorHasTag(TEXT("GreyboxVPSplatBuild"));
		const FString Label = Actor->GetActorNameOrLabel();
		const bool bLooksLikeGreybox =
			Label.Contains(TEXT("Ground")) ||
			Label.Contains(TEXT("Wall_")) ||
			Label.Contains(TEXT("Pillar")) ||
			Label.Contains(TEXT("Ramp"));

		if (bTaggedGreybox || bLooksLikeGreybox)
		{
			++Result.GreyboxActorCount;
		}
	}

	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Result.bHasSkyActors = true;
		break;
	}
	if (!Result.bHasSkyActors)
	{
		for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
		{
			Result.bHasSkyActors = true;
			break;
		}
	}
	if (!Result.bHasSkyActors)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			Result.bHasSkyActors = true;
			break;
		}
	}

	return Result;
}

FString FWorldLabsPromptBuilder::BuildPrompt(
	const FString& Environment,
	const FString& TimeOfDay,
	const FString& Mood,
	const FString& AdditionalNotes,
	const FWorldLabsSceneAnalysis& SceneAnalysis)
{
	const FString Notes = AdditionalNotes.TrimStartAndEnd();
	const FString GeometryContext = SceneAnalysis.GreyboxActorCount > 0
		? FString::Printf(
			TEXT("The scene contains %d detected greybox structural elements. Treat them as walls, platforms, and architectural forms integrated into a larger open world."),
			SceneAnalysis.GreyboxActorCount)
		: TEXT("No reliable greybox geometry metadata was detected. Preserve current composition while expanding to an open environment.");

	const FString SkyContext = SceneAnalysis.bHasSkyActors
		? TEXT("Use natural sky lighting and atmospheric depth with visible open sky.")
		: TEXT("Ensure strong open-sky readability and natural exterior lighting.");

	// Sky protection block (always applied to all presets)
	const FString SkyProtection = TEXT(
		"CRITICAL: The upper 40% of the equirectangular image (sky region) must be treated as open sky/atmosphere only. "
		"Any geometric shapes visible in the sky region are sensor artifacts from the 360 camera rig - ignore them completely. "
		"Generate natural sky, clouds, sun, atmosphere in the upper hemisphere. "
		"All structures exist only in the lower 60% of the scene (ground level).");

	const FString UpperHemisphereNoGeometry = TEXT(
		"The upper hemisphere (sky area) contains NO geometry - only open sky should be visible above the horizon line. "
		"Any dark shapes visible in the upper half of the reference image are capture artifacts and should be IGNORED and replaced with sky.");

	const FString GeometryInstructions = FString::Printf(
		TEXT("%s\n%s\n%s\nAvoid generating a closed six-sided room. Keep the upper hemisphere open to sky and add distant depth cues such as horizon detail, terrain, or cityscape through openings."),
		*SkyProtection,
		*UpperHemisphereNoGeometry,
		*GeometryContext);

	FString Prompt = FString::Printf(
		TEXT("Transform this architectural greybox into a photorealistic open environment.\n")
		TEXT("Environment preset: %s.\n")
		TEXT("Time of day: %s.\n")
		TEXT("Mood: %s.\n\n")
		TEXT("%s\n\n")
		TEXT("%s"),
		*Environment,
		*TimeOfDay,
		*Mood,
		*GeometryInstructions,
		*SkyContext);

	if (!Notes.IsEmpty())
	{
		Prompt += FString::Printf(TEXT("\nAdditional notes: %s"), *Notes);
	}

	return Prompt;
}
