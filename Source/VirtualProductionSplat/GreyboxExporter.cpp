// Copyright Epic Games, Inc. All Rights Reserved.

#include "GreyboxExporter.h"
#include "VirtualProductionSplat.h"
#include "MultiAngleCameraRig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

UGreyboxExporter::UGreyboxExporter() = default;

UWorld* GetGreyboxExportWorldForExporter()
{
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
			{
				return Ctx.World();
			}
		}
	}
#if WITH_EDITOR
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
#endif
	return nullptr;
}

void UGreyboxExporter::ExportAllCameras()
{
	UWorld* World = GetGreyboxExportWorldForExporter();
	if (!World)
	{
		UE_LOG(LogVPSplat, Error, TEXT("GreyboxExporter: no world context."));
		LastExportedFilePaths.Reset();
		return;
	}

	APanoramicCapture360* Pano = nullptr;
	for (TActorIterator<APanoramicCapture360> It(World); It; ++It)
	{
		Pano = *It;
		break;
	}

	if (!Pano)
	{
		UE_LOG(LogVPSplat, Error, TEXT("GreyboxExporter: no APanoramicCapture360 in level."));
		LastExportedFilePaths.Reset();
		return;
	}

	Pano->CaptureFaces();
	LastExportedFilePaths = Pano->SavedFacePaths;
	UE_LOG(LogVPSplat, Log, TEXT("GreyboxExporter: face capture invoked (%d face file(s))."), LastExportedFilePaths.Num());
}

bool UGreyboxExporter::SaveRenderTargetToPNG(UTextureRenderTarget2D* RT, const FString& FilePath)
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
