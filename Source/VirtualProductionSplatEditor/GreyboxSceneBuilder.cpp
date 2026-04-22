// Copyright Epic Games, Inc. All Rights Reserved.

#include "GreyboxSceneBuilder.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Components/LightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CineCameraActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

namespace GreyboxSceneBuilderInternal
{
	static const FName BuildTag(TEXT("GreyboxVPSplatBuild"));
	static const FName VPSunLightTag(TEXT("VPSunLight"));

	static UMaterialInterface* EnsureGreyboxMaterial()
	{
		const FString AssetPath = TEXT("/Game/Greybox/M_Greybox.M_Greybox");
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			return LoadObject<UMaterialInterface>(nullptr, *AssetPath);
		}

		UEditorAssetLibrary::MakeDirectory(TEXT("/Game/Greybox"));
		UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
			TEXT("/Game/Greybox/M_Greybox"));
		if (Duplicated)
		{
			return Cast<UMaterialInterface>(Duplicated);
		}

		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	static void DestroyPreviousGreybox(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TArray<AActor*> ToDestroy;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(BuildTag))
			{
				ToDestroy.Add(*It);
			}
		}
		for (AActor* A : ToDestroy)
		{
			if (A)
			{
				A->Destroy();
			}
		}
	}

	static AStaticMeshActor* SpawnCube(
		UWorld* World,
		UStaticMesh* CubeMesh,
		UMaterialInterface* Mat,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale,
		const FString& Label)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Location, Scale), Params);
		if (!Actor)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for %s."), *Label);
			return nullptr;
		}
		Actor->Tags.Add(BuildTag);
#if WITH_EDITOR
		Actor->SetActorLabel(Label);
#endif
		if (UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(CubeMesh);
			if (Mat)
			{
				SMC->SetMaterial(0, Mat);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned StaticMeshActor %s."), *Label);
		return Actor;
	}

	static AStaticMeshActor* SpawnPlane(
		UWorld* World,
		UStaticMesh* PlaneMesh,
		UMaterialInterface* Mat,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale,
		const FString& Label)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Location, Scale), Params);
		if (!Actor)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for plane %s."), *Label);
			return nullptr;
		}
		Actor->Tags.Add(BuildTag);
#if WITH_EDITOR
		Actor->SetActorLabel(Label);
#endif
		if (UStaticMeshComponent* SMC = Actor->GetStaticMeshComponent())
		{
			SMC->SetStaticMesh(PlaneMesh);
			if (Mat)
			{
				SMC->SetMaterial(0, Mat);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned horizon plane %s."), *Label);
		return Actor;
	}

	static void ClampActorTopZ(
		AActor* Actor,
		float TriggerTopZ = 250.0f,
		float DesiredTopZ = 200.0f)
	{
		if (!Actor)
		{
			return;
		}

		const FBox Bounds = Actor->GetComponentsBoundingBox();
		if (!Bounds.IsValid)
		{
			return;
		}

		if (Bounds.Max.Z <= TriggerTopZ)
		{
			return;
		}

		const float CurrentHeight = Bounds.Max.Z - Bounds.Min.Z;
		(void)CurrentHeight; // kept for log/debug parity with requested validation logic

		// Scale down Z so the top approaches DesiredTopZ.
		// (This matches the requested implementation pattern.)
		float Scale = Actor->GetActorScale3D().Z * (DesiredTopZ / Bounds.Max.Z);
		FVector NewScale = Actor->GetActorScale3D();
		NewScale.Z = Scale;
		Actor->SetActorScale3D(NewScale);

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("GreyboxSceneBuilder: %s top at Z=%.0f exceeds limit (%.0f). Scaling down so top targets %.0f."),
			*Actor->GetActorLabel(),
			Bounds.Max.Z,
			TriggerTopZ,
			DesiredTopZ);
	}

	static UMaterialInterface* EnsureHorizonMaterial()
	{
		const FString AssetPath = TEXT("/Game/Greybox/M_Horizon.M_Horizon");
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			return LoadObject<UMaterialInterface>(nullptr, *AssetPath);
		}

		if (!UEditorAssetLibrary::DoesAssetExist(TEXT("/Game/Greybox/M_Greybox.M_Greybox")))
		{
			return nullptr;
		}

		UEditorAssetLibrary::MakeDirectory(TEXT("/Game/Greybox"));
		UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(
			TEXT("/Game/Greybox/M_Greybox.M_Greybox"),
			TEXT("/Game/Greybox/M_Horizon"));
		if (Duplicated)
		{
			return Cast<UMaterialInterface>(Duplicated);
		}

		return nullptr;
	}

	static void TryAssignDirectionalLightToSkySphere(AActor* SkySphere, AActor* DirLight, UClass* SkySphereClass)
	{
		if (!SkySphere || !DirLight || !SkySphereClass)
		{
			return;
		}
		static const FName CandidateNames[] = {
			FName(TEXT("DirectionalLightActor")),
			FName(TEXT("DirectionalLight")),
			FName(TEXT("LightSource")),
		};
		for (const FName& PropName : CandidateNames)
		{
			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(SkySphereClass->FindPropertyByName(PropName)))
			{
				if (ObjProp->PropertyClass && DirLight->IsA(ObjProp->PropertyClass))
				{
					ObjProp->SetObjectPropertyValue_InContainer(SkySphere, DirLight);
					UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: BP_Sky_Sphere — set property %s."), *PropName.ToString());
					return;
				}
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: BP_Sky_Sphere — could not find DirectionalLight object property on class."));
	}

	static void SpawnSkySphereIfNeeded(UWorld* World, ADirectionalLight* DirLight)
	{
		if (!World)
		{
			return;
		}

		UClass* SkySphereClass = LoadObject<UClass>(nullptr, TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere_C"));
		if (!SkySphereClass)
		{
			SkySphereClass = LoadObject<UClass>(nullptr, TEXT("/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere"));
		}
		if (!SkySphereClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: BP_Sky_Sphere class not found — skipping sky sphere."));
			return;
		}

		TArray<AActor*> Existing;
		UGameplayStatics::GetAllActorsOfClass(World, SkySphereClass, Existing);
		if (Existing.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: BP_Sky_Sphere already in level (%d) — skipping spawn."), Existing.Num());
			return;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SkySphere = World->SpawnActor<AActor>(SkySphereClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (!SkySphere)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for BP_Sky_Sphere."));
			return;
		}

		SkySphere->Tags.Add(BuildTag);
#if WITH_EDITOR
		SkySphere->SetActorLabel(TEXT("SkySphere_VP"));
#endif
		SkySphere->SetActorScale3D(FVector(100.f));

		if (DirLight)
		{
			TryAssignDirectionalLightToSkySphere(SkySphere, DirLight, SkySphereClass);
		}

		UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned BP_Sky_Sphere (SkySphere_VP)."));
	}
}

void AGreyboxSceneBuilder::BuildGreyboxScene()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !World->IsEditorWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: open a level in the editor (not PIE) and place this actor in the level."));
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("VPSplat", "BuildGreyboxScene", "Build Greybox Scene"));

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: failed to load engine cube mesh."));
		return;
	}

	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: failed to load engine plane mesh."));
		return;
	}

	UMaterialInterface* Mat = GreyboxSceneBuilderInternal::EnsureGreyboxMaterial();
	GreyboxSceneBuilderInternal::DestroyPreviousGreybox(World);

	UMaterialInterface* HorizonMat = GreyboxSceneBuilderInternal::EnsureHorizonMaterial();
	if (!HorizonMat)
	{
		HorizonMat = Mat;
	}

	// Ground: 2000 x 2000 x 10 (cube 100uu → scale 20, 20, 0.1), centered so top at Z=0
	AStaticMeshActor* GroundActor = GreyboxSceneBuilderInternal::SpawnCube(
		World, CubeMesh, Mat, FVector(0.f, 0.f, -5.f), FRotator::ZeroRotator, FVector(20.f, 20.f, 0.1f), TEXT("Ground"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(GroundActor);

	// Distant horizon plane (open toward -Y; gives depth cue for panorama / Marble)
	AStaticMeshActor* HorizonPlaneActor = GreyboxSceneBuilderInternal::SpawnPlane(
		World,
		PlaneMesh,
		HorizonMat,
		FVector(0.f, 0.f, -500.f),
		FRotator::ZeroRotator,
		FVector(200.f, 200.f, 1.f),
		TEXT("HorizonPlane"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(HorizonPlaneActor);

	// U-shaped walls: North + East + West only (South open to horizon). No ceiling.
	// Lower geometry so the upper hemisphere contains only sky.
	// Cube mesh size is 100uu, so wall top Z ~= Location.Z + 50*Scale.Z.
	AStaticMeshActor* WallNorthActor = GreyboxSceneBuilderInternal::SpawnCube(
		World, CubeMesh, Mat, FVector(0.f, 1000.f, 100.f), FRotator::ZeroRotator, FVector(20.f, 0.2f, 2.f), TEXT("Wall_North"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(WallNorthActor);

	AStaticMeshActor* WallEastActor = GreyboxSceneBuilderInternal::SpawnCube(
		World, CubeMesh, Mat, FVector(1000.f, 0.f, 100.f), FRotator::ZeroRotator, FVector(0.2f, 20.f, 2.f), TEXT("Wall_East"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(WallEastActor);

	AStaticMeshActor* WallWestActor = GreyboxSceneBuilderInternal::SpawnCube(
		World, CubeMesh, Mat, FVector(-1000.f, 0.f, 100.f), FRotator::ZeroRotator, FVector(0.2f, 20.f, 2.f), TEXT("Wall_West"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(WallWestActor);

	AStaticMeshActor* PillarActor = GreyboxSceneBuilderInternal::SpawnCube(
		World, CubeMesh, Mat, FVector(0.f, 400.f, 100.f), FRotator::ZeroRotator, FVector(2.f, 2.f, 2.f), TEXT("Pillar"));
	GreyboxSceneBuilderInternal::ClampActorTopZ(PillarActor);

	// Ramp removed (it created an angled dark band in the upper hemisphere).

	ADirectionalLight* SunLight = nullptr;

	// Directional light (always spawned after destroy — tagged builds)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SunLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-60.f, -135.f, 0.f), FVector::ZeroVector), Params);
		if (!SunLight)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for ADirectionalLight."));
		}
		else
		{
			SunLight->Tags.Add(GreyboxSceneBuilderInternal::BuildTag);
			SunLight->Tags.Add(GreyboxSceneBuilderInternal::VPSunLightTag);
#if WITH_EDITOR
			SunLight->SetActorLabel(TEXT("SunLight"));
#endif
			if (ULightComponent* LC = SunLight->GetLightComponent())
			{
				LC->SetIntensity(10.f);
				LC->SetMobility(EComponentMobility::Movable);
				LC->SetCastShadows(true);
			}
			if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
			{
				DLC->SetAtmosphereSunLight(true);
				DLC->SetForwardShadingPriority(1);
			}
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned ADirectionalLight (SunLight, tag VPSunLight)."));
		}
	}

	// Sky light — always spawn (no stale AnyActorOfClass in world; previous build actors were destroyed)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, Params);
		if (!Sky)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for ASkyLight."));
		}
		else
		{
			Sky->Tags.Add(GreyboxSceneBuilderInternal::BuildTag);
#if WITH_EDITOR
			Sky->SetActorLabel(TEXT("SkyLight_VP"));
#endif
			if (USkyLightComponent* SLC = Cast<USkyLightComponent>(Sky->GetLightComponent()))
			{
				SLC->SetIntensity(1.0f);
				SLC->SetMobility(EComponentMobility::Movable);
				SLC->bRealTimeCapture = true;
			}
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned ASkyLight (SkyLight_VP)."));
		}
	}

	// Sky atmosphere
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASkyAtmosphere* Atmo = World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Params);
		if (!Atmo)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for ASkyAtmosphere."));
		}
		else
		{
			Atmo->Tags.Add(GreyboxSceneBuilderInternal::BuildTag);
#if WITH_EDITOR
			Atmo->SetActorLabel(TEXT("SkyAtmosphere"));
#endif
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned ASkyAtmosphere."));
		}
	}

	// Exponential height fog
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, Params);
		if (!Fog)
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for AExponentialHeightFog."));
		}
		else
		{
			Fog->Tags.Add(GreyboxSceneBuilderInternal::BuildTag);
#if WITH_EDITOR
			Fog->SetActorLabel(TEXT("HeightFog_VP"));
#endif
			if (UExponentialHeightFogComponent* FogComp = Fog->GetComponent())
			{
				FogComp->SetFogDensity(0.02f);
				FogComp->SetFogHeightFalloff(0.2f);
			}
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned AExponentialHeightFog (HeightFog_VP)."));
		}
	}

	// BP_Sky_Sphere (engine sky) — ties to directional light when possible
	GreyboxSceneBuilderInternal::SpawnSkySphereIfNeeded(World, SunLight);

	// Primary cine camera
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector CamLoc(0.f, -600.f, 150.f);
		const FRotator CamRot = UKismetMathLibrary::FindLookAtRotation(CamLoc, FVector::ZeroVector);
		if (ACineCameraActor* Cam = World->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), FTransform(CamRot, CamLoc), Params))
		{
			Cam->Tags.Add(GreyboxSceneBuilderInternal::BuildTag);
#if WITH_EDITOR
			Cam->SetActorLabel(TEXT("PrimaryCamera"));
#endif
			UE_LOG(LogTemp, Warning, TEXT("GreyboxSceneBuilder: spawned ACineCameraActor (PrimaryCamera)."));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("GreyboxSceneBuilder: SpawnActor failed for ACineCameraActor."));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("GreyboxSceneBuilder: BuildGreyboxScene finished (material /Game/Greybox/M_Greybox if created)."));
}
