#include "WorldLabsPromptBuilder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

FSceneAnalysisResult FWorldLabsPromptBuilder::AnalyzeScene(UWorld* World)
{
	FSceneAnalysisResult Result;
	if (!World) return Result;

	TArray<AActor*> GreyboxActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->ActorHasTag(TEXT("GreyboxVPSplatBuild")))
			GreyboxActors.Add(Actor);
	}
	Result.GreyboxActorCount = GreyboxActors.Num();
	if (GreyboxActors.Num() == 0) return Result;

	// Sky-dome / atmosphere actors have engine-scale bounds (millions of cm) that wreck
	// the scene footprint. Exclude anything whose label or tags read as sky/atmosphere.
	auto IsSkyDomeActor = [](const AActor* A) -> bool
	{
		static const TCHAR* SkyTokens[] = { TEXT("sky"), TEXT("horizon"), TEXT("atmosphere"), TEXT("fog"), TEXT("sun") };
		const FString Name = A->GetActorLabel().ToLower();
		for (const TCHAR* Tok : SkyTokens)
			if (Name.Contains(Tok)) return true;
		for (const FName& Tag : A->Tags)
		{
			const FString T = Tag.ToString().ToLower();
			for (const TCHAR* Tok : SkyTokens)
				if (T.Contains(Tok)) return true;
		}
		return false;
	};

	FBox SceneBox(EForceInit::ForceInit);
	int32 ExcludedSkyActors = 0;
	for (AActor* A : GreyboxActors)
	{
		if (IsSkyDomeActor(A)) { ExcludedSkyActors++; continue; }
		SceneBox += A->GetComponentsBoundingBox(true);
	}
	if (ExcludedSkyActors > 0)
		UE_LOG(LogTemp, Warning, TEXT("PromptBuilder: excluded %d sky/atmosphere actor(s) from scene bounds."), ExcludedSkyActors);

	if (!SceneBox.IsValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("PromptBuilder: no usable bounds after sky exclusion — skipping spatial analysis."));
		return Result;
	}

	Result.SceneBoundsMin = SceneBox.Min;
	Result.SceneBoundsMax = SceneBox.Max;
	Result.SceneExtent    = SceneBox.GetExtent() * 2.f;
	Result.SceneCenter    = SceneBox.GetCenter();
	Result.EstimatedFloorZ = SceneBox.Min.Z;

	Result.DominantHorizontalExtentM =
		FMath::Max(Result.SceneExtent.X, Result.SceneExtent.Y) / 100.f;
	Result.VerticalExtentM = Result.SceneExtent.Z / 100.f;

	for (AActor* A : GreyboxActors)
	{
		FString Name = A->GetActorLabel().ToLower();
		if (Name.Contains(TEXT("wall")) || Name.Contains(TEXT("facade")))
			Result.WallActorCount++;
		else if (Name.Contains(TEXT("floor")) || Name.Contains(TEXT("ground"))
			  || Name.Contains(TEXT("plane")))
			Result.FloorActorCount++;
		else
			Result.PropActorCount++;
	}

	float CeilingThreshold = Result.SceneBoundsMin.Z + Result.SceneExtent.Z * 0.8f;
	for (AActor* A : GreyboxActors)
		if (A->GetActorLocation().Z > CeilingThreshold)
		{ Result.bHasCeiling = true; break; }

	float CenterX = Result.SceneCenter.X;
	float CenterY = Result.SceneCenter.Y;
	bool bWallNorth=false, bWallSouth=false, bWallEast=false, bWallWest=false;
	for (AActor* A : GreyboxActors)
	{
		FVector L = A->GetActorLocation();
		if (L.Y > CenterY + 200) bWallNorth = true;
		if (L.Y < CenterY - 200) bWallSouth = true;
		if (L.X > CenterX + 200) bWallEast  = true;
		if (L.X < CenterX - 200) bWallWest  = true;
	}
	int32 SidesWithWalls = (bWallNorth?1:0)+(bWallSouth?1:0)+
	                       (bWallEast?1:0)+(bWallWest?1:0);
	Result.bIsInteriorSpace = (SidesWithWalls >= 3);
	Result.bHasOpenHorizon  = (Result.DominantHorizontalExtentM > 20.f);

	float Ratio = Result.SceneExtent.X / FMath::Max(Result.SceneExtent.Y, 1.f);
	if (Ratio > 2.5f || Ratio < 0.4f)
		Result.DominantShape = TEXT("linear corridor");
	else if (SidesWithWalls == 2 &&
	         (bWallNorth || bWallSouth) && (bWallEast || bWallWest))
		Result.DominantShape = TEXT("L-shaped space");
	else if (SidesWithWalls == 3)
		Result.DominantShape = TEXT("U-shaped courtyard");
	else if (SidesWithWalls == 4)
		Result.DominantShape = TEXT("enclosed room");
	else
		Result.DominantShape = TEXT("open field");

	Result.SpatialDescription = FString::Printf(
		TEXT("%.0f x %.0f meters, %.0f meters tall, %s, %d walls, %s horizon, %s"),
		Result.SceneExtent.X / 100.f,
		Result.SceneExtent.Y / 100.f,
		Result.VerticalExtentM,
		*Result.DominantShape,
		Result.WallActorCount,
		Result.bHasOpenHorizon ? TEXT("open") : TEXT("tight"),
		Result.bHasCeiling ? TEXT("ceiling present") : TEXT("no ceiling")
	);

	UE_LOG(LogTemp, Warning, TEXT("PromptBuilder: %s"), *Result.SpatialDescription);
	return Result;
}

FString FWorldLabsPromptBuilder::BuildPrompt(
	const FString& Environment,
	const FString& TimeOfDay,
	const FString& Mood,
	const FString& AdditionalNotes,
	const FSceneAnalysisResult& SceneAnalysis)
{
	const FString Notes = AdditionalNotes.TrimStartAndEnd();

	// Spatial constraints block — injected from live scene analysis
	FString SpatialBlock;

	if (SceneAnalysis.bIsInteriorSpace)
		SpatialBlock += TEXT(
			"SPATIAL CONTEXT: This is an interior-style layout. "
			"Generate a space that feels enclosed but architecturally "
			"rich — courtyards, ruins, cave chambers, or walled gardens. ");
	else
		SpatialBlock += TEXT(
			"SPATIAL CONTEXT: This is an open layout. "
			"Generate a vast outdoor environment with a visible horizon. "
			"No walls, no ceilings. Sky must be visible. ");

	if (!SceneAnalysis.SpatialDescription.IsEmpty())
		SpatialBlock += FString::Printf(
			TEXT("Scene footprint: %s. "),
			*SceneAnalysis.SpatialDescription);

	if (SceneAnalysis.bHasCeiling)
		SpatialBlock += TEXT(
			"IMPORTANT: A ceiling structure is present — "
			"generate a natural overhead element like a cave roof, "
			"forest canopy, or overhanging cliff. Do NOT leave the sky open. ");
	else
		SpatialBlock += TEXT(
			"IMPORTANT: No ceiling. Upper hemisphere must show open sky, "
			"clouds, or stars. ");

	if (SceneAnalysis.VerticalExtentM > 0.f)
		SpatialBlock += FString::Printf(
			TEXT("Camera eye height is approximately %.1f meters above floor. "
			     "Ensure ground surface is visible and textured. "),
			(SceneAnalysis.SceneCenter.Z - SceneAnalysis.EstimatedFloorZ) / 100.f);

	const FString GeometryContext = SceneAnalysis.GreyboxActorCount > 0
		? FString::Printf(
			TEXT("The scene contains %d detected greybox structural elements. Treat them as walls, platforms, and architectural forms integrated into a larger open world."),
			SceneAnalysis.GreyboxActorCount)
		: TEXT("No reliable greybox geometry metadata was detected. Preserve current composition while expanding to an open environment.");

	const FString SkyContext = SceneAnalysis.bHasSkyActors
		? TEXT("Use natural sky lighting and atmospheric depth with visible open sky.")
		: TEXT("Ensure strong open-sky readability and natural exterior lighting.");

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

	FString Prompt = SpatialBlock + TEXT("\n\n");
	Prompt += FString::Printf(
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
