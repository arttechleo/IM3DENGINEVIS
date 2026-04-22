// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLSGaussianSplatInterop.h"
#include "VirtualProductionSplat.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if PLATFORM_WINDOWS

static bool SetStringPropertyIfPresent(UActorComponent* Comp, FName PropName, const FString& Value)
{
	if (!Comp)
	{
		return false;
	}
	FProperty* Prop = Comp->GetClass()->FindPropertyByName(PropName);
	if (FStrProperty* SP = CastField<FStrProperty>(Prop))
	{
		if (FString* Mutable = SP->ContainerPtrToValuePtr<FString>(Comp))
		{
			*Mutable = Value;
			return true;
		}
	}
	return false;
}

UClass* FMLSGaussianSplatInterop::GetGaussianSplattingActorClass()
{
	if (UClass* Found = FindObject<UClass>(nullptr, TEXT("/Script/MLSLabsRenderer.GaussianSplattingActor")))
	{
		return Found;
	}
	return LoadClass<AActor>(nullptr, TEXT("/Script/MLSLabsRenderer.GaussianSplattingActor"));
}

UClass* FMLSGaussianSplatInterop::GetGaussianSplattingComponentClass()
{
	if (UClass* Found = FindObject<UClass>(nullptr, TEXT("/Script/MLSLabsRenderer.GaussianSplattingComponent")))
	{
		return Found;
	}
	return LoadClass<UActorComponent>(nullptr, TEXT("/Script/MLSLabsRenderer.GaussianSplattingComponent"));
}

UActorComponent* FMLSGaussianSplatInterop::FindGaussianSplattingComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UClass* CompClass = GetGaussianSplattingComponentClass())
	{
		if (UActorComponent* C = Actor->GetComponentByClass(CompClass))
		{
			return C;
		}
	}
	for (UActorComponent* C : Actor->GetComponents())
	{
		if (C && C->GetClass()->GetName() == TEXT("GaussianSplattingComponent"))
		{
			return C;
		}
	}
	return nullptr;
}

bool FMLSGaussianSplatInterop::SetPrimaryPlyPathProperty(UActorComponent* GaussianComp, const FString& AbsolutePlyPath)
{
	if (!GaussianComp)
	{
		return false;
	}

	// Try full-path properties first.
	if (SetStringPropertyIfPresent(GaussianComp, FName(TEXT("SplatDataPath")), AbsolutePlyPath))
	{
		UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: set SplatDataPath = %s"), *AbsolutePlyPath);
	}

	// Also populate dir+filename split — plugin checks these separately on some versions.
	const FString Dir  = FPaths::GetPath(AbsolutePlyPath);
	const FString File = FPaths::GetCleanFilename(AbsolutePlyPath);
	SetStringPropertyIfPresent(GaussianComp, FName(TEXT("SplatFileDirName")), Dir);
	SetStringPropertyIfPresent(GaussianComp, FName(TEXT("SplatFileName")),    File);

	// Fallback: some releases accept the full path in SplatFileName directly.
	if (SetStringPropertyIfPresent(GaussianComp, FName(TEXT("SplatFileName")), AbsolutePlyPath))
	{
		UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: set SplatFileName = %s"), *AbsolutePlyPath);
	}

	// Confirm at least SplatDataPath or SplatFileName was accepted.
	FProperty* P1 = GaussianComp->GetClass()->FindPropertyByName(FName(TEXT("SplatDataPath")));
	FProperty* P2 = GaussianComp->GetClass()->FindPropertyByName(FName(TEXT("SplatFileName")));
	return (P1 != nullptr || P2 != nullptr);
}

bool FMLSGaussianSplatInterop::QueueLoadSplatData(UActorComponent* GaussianComp)
{
	if (!GaussianComp)
	{
		return false;
	}

	// Try every known UFUNCTION/native name the plugin has used across versions.
	static const TCHAR* LoadFnNames[] = {
		TEXT("QueueLoadSplatData"),
		TEXT("LoadSplatFile"),
		TEXT("LoadGaussianSplatFileCommand"),
		TEXT("ReloadSplatData"),
		TEXT("BeginLoadSplat"),
	};
	for (const TCHAR* FnName : LoadFnNames)
	{
		if (UFunction* Fn = GaussianComp->FindFunction(FName(FnName)))
		{
			GaussianComp->ProcessEvent(Fn, nullptr);
			UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: triggered load via UFUNCTION '%s'"), FnName);
			return true;
		}
	}

	// No UFUNCTION found — QueueLoadSplatData is a non-reflected C++ method in this build.
	// Notify the component via PostEditChangeProperty so it reacts to the new SplatDataPath,
	// then dirty the render state to force a redraw once data arrives.
	UE_LOG(LogVPSplat, Warning,
		TEXT("MLSGaussianSplatInterop: no load UFUNCTION on %s — using PostEditChangeProperty + MarkRenderStateDirty fallback"),
		*GaussianComp->GetClass()->GetName());

#if WITH_EDITOR
	for (const TCHAR* PropName : { TEXT("SplatDataPath"), TEXT("SplatFileName"), TEXT("SplatFileDirName") })
	{
		if (FProperty* Prop = GaussianComp->GetClass()->FindPropertyByName(FName(PropName)))
		{
			FPropertyChangedEvent ChangeEvent(Prop, EPropertyChangeType::ValueSet);
			GaussianComp->PostEditChangeProperty(ChangeEvent);
		}
	}
#endif

	if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(GaussianComp))
	{
		Prim->MarkRenderStateDirty();
	}

	// Re-register to trigger OnRegister/BeginPlay load path.
	if (GaussianComp->IsRegistered())
	{
		GaussianComp->UnregisterComponent();
		GaussianComp->RegisterComponent();
	}

	return true; // Path was set; load will proceed asynchronously.
}

bool FMLSGaussianSplatInterop::RefreshBoundsFromLoadedSplat(UActorComponent* GaussianComp)
{
	if (!GaussianComp)
	{
		return false;
	}
	if (UFunction* Fn = GaussianComp->FindFunction(FName(TEXT("RefreshBoundsFromLoadedSplat"))))
	{
		GaussianComp->ProcessEvent(Fn, nullptr);
		return true;
	}
	return false;
}

static bool ApplyPlyAndQueue(AActor* Actor, const FString& AbsolutePlyPath, FString& OutError)
{
	UActorComponent* Comp = FMLSGaussianSplatInterop::FindGaussianSplattingComponent(Actor);
	if (!Comp)
	{
		OutError = TEXT("GaussianSplattingComponent not found on actor.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return false;
	}
	if (!FMLSGaussianSplatInterop::SetPrimaryPlyPathProperty(Comp, AbsolutePlyPath))
	{
		OutError = TEXT("Could not set SplatDataPath / SplatFileName on GaussianSplattingComponent.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return false;
	}
	if (!FMLSGaussianSplatInterop::QueueLoadSplatData(Comp))
	{
		OutError = TEXT("QueueLoadSplatData failed.");
		return false;
	}
	FMLSGaussianSplatInterop::RefreshBoundsFromLoadedSplat(Comp);
	return true;
}

AActor* FMLSGaussianSplatInterop::SpawnGaussianSplatAt(
	UWorld* World,
	const FString& AbsolutePlyPath,
	const FTransform& WorldTransform,
	float UniformScale,
	FString& OutError)
{
	UClass* ActorClass = GetGaussianSplattingActorClass();
	if (!World || !ActorClass)
	{
		OutError = TEXT("MLSLabsRenderer not loaded or AGaussianSplattingActor class missing (Win64 editor only).");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor(ActorClass, &WorldTransform, Params);
	if (!NewActor)
	{
		OutError = TEXT("SpawnActor(AGaussianSplattingActor) failed.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	const float S = FMath::Max(UniformScale, KINDA_SMALL_NUMBER);
	NewActor->SetActorScale3D(FVector(S));

	if (!ApplyPlyAndQueue(NewActor, AbsolutePlyPath, OutError))
	{
		NewActor->Destroy();
		return nullptr;
	}

	UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: spawned Gaussian splat at %s"), *AbsolutePlyPath);
	return NewActor;
}

AActor* FMLSGaussianSplatInterop::SpawnOrReloadWorldLabsSplat(
	UWorld* World,
	const FString& AbsolutePlyPath,
	float UniformScale,
	const FVector& SpawnLocation,
	FString& OutError)
{
	UClass* ActorClass = GetGaussianSplattingActorClass();
	if (!World || !ActorClass)
	{
		OutError = TEXT("MLSLabsRenderer not loaded or AGaussianSplattingActor class missing (Win64 editor only).");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	AActor* Existing = nullptr;
	for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
	{
		if (It->GetActorLabel() == TEXT("WorldLabs_Splat"))
		{
			Existing = *It;
			break;
		}
	}

	const float S = FMath::Max(UniformScale, KINDA_SMALL_NUMBER);

	if (Existing)
	{
		Existing->SetActorLocation(SpawnLocation);
		Existing->SetActorScale3D(FVector(S));
		if (!ApplyPlyAndQueue(Existing, AbsolutePlyPath, OutError))
		{
			return nullptr;
		}
		UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: reloaded WorldLabs_Splat"));
		return Existing;
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor(ActorClass, &SpawnTransform, Params);
	if (!NewActor)
	{
		OutError = TEXT("SpawnActor(AGaussianSplattingActor) failed.");
		UE_LOG(LogVPSplat, Error, TEXT("MLSGaussianSplatInterop: %s"), *OutError);
		return nullptr;
	}

	NewActor->SetActorLabel(TEXT("WorldLabs_Splat"));
	NewActor->SetActorScale3D(FVector(S));

	if (!ApplyPlyAndQueue(NewActor, AbsolutePlyPath, OutError))
	{
		NewActor->Destroy();
		return nullptr;
	}

	UE_LOG(LogVPSplat, Log, TEXT("MLSGaussianSplatInterop: spawned WorldLabs_Splat at %s"), *SpawnLocation.ToString());
	return NewActor;
}

bool FMLSGaussianSplatInterop::WorldHasMLSGaussianActor(UWorld* World)
{
	if (!World)
	{
		return false;
	}
	UClass* ActorClass = GetGaussianSplattingActorClass();
	if (!ActorClass)
	{
		return false;
	}
	for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
	{
		return true;
	}
	return false;
}

#else // !PLATFORM_WINDOWS

UClass* FMLSGaussianSplatInterop::GetGaussianSplattingActorClass()
{
	return nullptr;
}
UClass* FMLSGaussianSplatInterop::GetGaussianSplattingComponentClass()
{
	return nullptr;
}
UActorComponent* FMLSGaussianSplatInterop::FindGaussianSplattingComponent(AActor* Actor)
{
	return nullptr;
}
bool FMLSGaussianSplatInterop::SetPrimaryPlyPathProperty(UActorComponent* GaussianComp, const FString& AbsolutePlyPath)
{
	return false;
}
bool FMLSGaussianSplatInterop::QueueLoadSplatData(UActorComponent* GaussianComp)
{
	return false;
}
bool FMLSGaussianSplatInterop::RefreshBoundsFromLoadedSplat(UActorComponent* GaussianComp)
{
	return false;
}
AActor* FMLSGaussianSplatInterop::SpawnGaussianSplatAt(
	UWorld* World,
	const FString& AbsolutePlyPath,
	const FTransform& WorldTransform,
	float UniformScale,
	FString& OutError)
{
	OutError = TEXT("MLSLabsRenderer is Win64-only.");
	return nullptr;
}
AActor* FMLSGaussianSplatInterop::SpawnOrReloadWorldLabsSplat(
	UWorld* World,
	const FString& AbsolutePlyPath,
	float UniformScale,
	const FVector& SpawnLocation,
	FString& OutError)
{
	OutError = TEXT("MLSLabsRenderer is Win64-only.");
	return nullptr;
}
bool FMLSGaussianSplatInterop::WorldHasMLSGaussianActor(UWorld* World)
{
	return false;
}

#endif // PLATFORM_WINDOWS
