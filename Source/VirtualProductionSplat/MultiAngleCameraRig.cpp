// Copyright Epic Games, Inc. All Rights Reserved.

#include "MultiAngleCameraRig.h"
#include "VirtualProductionSplat.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShowFlags.h"
#include "UObject/UObjectGlobals.h"

namespace PanoramicCapture360Internal
{
	static const TCHAR* FaceSuffix[6] = {
		TEXT("PosX"),
		TEXT("NegX"),
		TEXT("PosY"),
		TEXT("NegY"),
		TEXT("PosZ"),
		TEXT("NegZ"),
	};

	static const FRotator FaceRotations[6] = {
		FRotator(0.0, 0.0, 0.0),
		FRotator(0.0, 180.0, 0.0),
		FRotator(0.0, 90.0, 0.0),
		FRotator(0.0, -90.0, 0.0),
		FRotator(-90.0, 0.0, 0.0),
		FRotator(90.0, 0.0, 0.0),
	};
}

APanoramicCapture360::APanoramicCapture360()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CaptureComponents.SetNum(6);
	for (int32 i = 0; i < 6; ++i)
	{
		const FString CompName = FString::Printf(TEXT("Capture_%s"), PanoramicCapture360Internal::FaceSuffix[i]);
		USceneCaptureComponent2D* Cap = CreateDefaultSubobject<USceneCaptureComponent2D>(*CompName);
		Cap->SetupAttachment(Root);
		Cap->SetRelativeRotation(PanoramicCapture360Internal::FaceRotations[i]);
		Cap->FOVAngle = 90.0f;
		Cap->CaptureSource = SCS_FinalColorLDR;
		Cap->bCaptureEveryFrame = false;
		Cap->bCaptureOnMovement = false;
		Cap->ShowFlags.SetAtmosphere(true);
		Cap->ShowFlags.SetSkyLighting(true);
		Cap->ShowFlags.SetFog(true);
		Cap->SetHiddenInGame(false);
		Cap->HiddenActors.Add(this);
		Cap->ShowFlags.SetEditor(false);
		Cap->ShowFlags.SetModeWidgets(false);
		CaptureComponents[i] = Cap;
	}
}

void APanoramicCapture360::CaptureFaces()
{
	if (!GetWorld())
	{
		UE_LOG(LogVPSplat, Error, TEXT("CaptureFaces: no world"));
		return;
	}

	SavedFacePaths.Reset();

	FString OutDir = OutputDirectory.TrimStartAndEnd();
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GreyboxExports"), TEXT("faces"));
	}
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const int32 Res = FMath::Max(1, FaceResolution);

	for (int32 i = 0; i < 6; ++i)
	{
		if (!CaptureComponents.IsValidIndex(i) || !CaptureComponents[i])
		{
			UE_LOG(LogVPSplat, Error, TEXT("CaptureFaces: missing capture component index %d."), i);
			continue;
		}

		USceneCaptureComponent2D* const Cap = CaptureComponents[i].Get();

		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient);
		if (!RT)
		{
			UE_LOG(LogVPSplat, Error, TEXT("CaptureFaces: NewObject UTextureRenderTarget2D failed (face %d)."), i);
			continue;
		}

		RT->RenderTargetFormat = RTF_RGBA8;
		RT->ClearColor = FLinearColor::Black;
		RT->InitAutoFormat(static_cast<uint32>(Res), static_cast<uint32>(Res));
		RT->UpdateResourceImmediate(true);

		Cap->TextureTarget = RT;
		Cap->CaptureScene();
		FlushRenderingCommands();

		const FString FileName = FString::Printf(TEXT("face_%s.png"), PanoramicCapture360Internal::FaceSuffix[i]);
		const FString FilePath = FPaths::Combine(OutDir, FileName);
		const FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);

		const bool bSaved = SaveRenderTargetToPNG(RT, FullPath);
		if (!bSaved)
		{
			UE_LOG(LogVPSplat, Error, TEXT("PanoramicCapture360: failed to save face %s"), *FullPath);
		}
		else
		{
			UE_LOG(LogVPSplat, Log, TEXT("PanoramicCapture360: saved %s"), *FullPath);
			SavedFacePaths.Add(FullPath);
		}

		Cap->TextureTarget = nullptr;
	}

	LastSavedPath.Reset();
}

bool APanoramicCapture360::SaveRenderTargetToPNG(UTextureRenderTarget2D* RT, const FString& FilePath) const
{
	if (!RT)
	{
		return false;
	}

	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*FilePath));
	if (!Ar.IsValid())
	{
		return false;
	}

	return FImageUtils::ExportRenderTarget2DAsPNG(RT, *Ar);
}
