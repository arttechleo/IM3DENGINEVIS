// Copyright Epic Games, Inc. All Rights Reserved.

#include "GreyboxExportRunner.h"
#include "MultiAngleCameraRig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "IPythonScriptPlugin.h"

void APanoramicExportRunner::CapturePanorama()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("PanoramicExportRunner: no world."));
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
		UE_LOG(LogTemp, Error, TEXT("PanoramicExportRunner: no APanoramicCapture360 in level."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("PanoramicExportRunner: invoking APanoramicCapture360::CaptureFaces"));
	Pano->CaptureFaces();
}

void APanoramicExportRunner::StitchPanorama()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("PanoramicExportRunner: no world."));
		return;
	}

	APanoramicCapture360* Pano = nullptr;
	for (TActorIterator<APanoramicCapture360> It(World); It; ++It)
	{
		Pano = *It;
		break;
	}

	FString ScriptPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/VirtualProductionSplatEditor/StitchEquirectangular.py")));

	if (IPythonScriptPlugin* Py = IPythonScriptPlugin::Get())
	{
		if (!Py->IsPythonInitialized())
		{
			Py->ForceEnablePythonAtRuntime();
		}

		if (Py->IsPythonAvailable() && Py->IsPythonInitialized())
		{
			FString Normalized = ScriptPath;
			Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
			const FString Cmd = FString::Printf(
				TEXT("exec(open(r'%s', encoding='utf-8').read())"),
				*Normalized);

			if (Py->ExecPythonCommand(*Cmd))
			{
				const FString PanoOut = FPaths::ConvertRelativePathToFull(
					FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GreyboxExports"), TEXT("panorama_360.png")));
				if (Pano)
				{
					Pano->LastSavedPath = PanoOut;
				}
				UE_LOG(LogTemp, Log, TEXT("PanoramicExportRunner: StitchPanorama finished — %s"), *PanoOut);
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("PanoramicExportRunner: ExecPythonCommand failed; see Output Log."));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("PanoramicExportRunner: Python not available/initialized."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PanoramicExportRunner: PythonScriptPlugin not loaded."));
	}

	UE_LOG(LogTemp, Log, TEXT("PanoramicExportRunner: run Tools > Execute Python Script and choose: %s"), *ScriptPath);
}
