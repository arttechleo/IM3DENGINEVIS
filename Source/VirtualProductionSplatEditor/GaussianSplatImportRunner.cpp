// Copyright Epic Games, Inc. All Rights Reserved.

#include "GaussianSplatImportRunner.h"
#include "GaussianSplatImportHelper.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "Widgets/Notifications/SNotificationList.h"

/**
 * Run ConvertSpzToPly.py with the given Python binary.
 * OutLog receives the merged stdout+stderr so callers can surface it.
 * Returns true only when exit == 0 AND the output .ply exists on disk.
 */
static bool TryConvertSpzToPly(
	const FString& PythonPath,
	const FString& ScriptPath,
	const FString& SpzPath,
	const FString& PlyOutPath,
	FString& OutLog)
{
	const FString Params = FString::Printf(
		TEXT("\"%s\" \"%s\" \"%s\""), *ScriptPath, *SpzPath, *PlyOutPath);

	int32 ReturnCode = -1;
	FString StdOut, StdErr;
	const bool bOk = FPlatformProcess::ExecProcess(
		*PythonPath, *Params, &ReturnCode, &StdOut, &StdErr);

	// Merge stdout+stderr into one log string (Fix 3).
	OutLog = FString::Printf(
		TEXT("python=%s  exit=%d  process_ok=%d\n--- stdout ---\n%s\n--- stderr ---\n%s"),
		*PythonPath, ReturnCode, bOk ? 1 : 0, *StdOut, *StdErr);

	return bOk && ReturnCode == 0 && FPaths::FileExists(PlyOutPath);
}

AGaussianSplatImportRunner::AGaussianSplatImportRunner()
{
	PrimaryActorTick.bCanEverTick = false;
	PLYFilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("GaussianSplats"), TEXT("WorldLabs_export.ply")));
}

FTransform AGaussianSplatImportRunner::ComputeSpawnTransform() const
{
	if (bUseThisActorTransform)
	{
		return GetActorTransform();
	}
	return FTransform(SpawnRotation, SpawnLocation);
}

void AGaussianSplatImportRunner::ImportPLYIntoLevel()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("GaussianSplatImportRunner: no world."));
		return;
	}

	// ---- Step 1: Resolve PLY path (convert .spz to .ply if needed) ----
	FString ResolvedPlyPath = FPaths::ConvertRelativePathToFull(PLYFilePath);

	if (ResolvedPlyPath.EndsWith(TEXT(".spz"), ESearchCase::IgnoreCase))
	{
		const FString PlyOutPath = FPaths::ChangeExtension(ResolvedPlyPath, TEXT("ply"));

		if (!FPaths::FileExists(PlyOutPath))
		{
			const FString ScriptPath = FPaths::Combine(
				FPaths::ProjectDir(), TEXT("Source/VirtualProductionSplatEditor/ConvertSpzToPly.py"));
			if (!FPaths::FileExists(ScriptPath))
			{
				UE_LOG(LogTemp, Error,
					TEXT("GaussianSplatImportRunner: ConvertSpzToPly.py not found at %s"), *ScriptPath);
				PostImportToast(TEXT("Splat import failed: ConvertSpzToPly.py missing"), false);
				return;
			}

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(PlyOutPath), true);

			// Use UE's bundled Python. The script is pure-Python (numpy only) so
			// no external packages need to be installed.
#if PLATFORM_WINDOWS
			FString UE_Python = FPaths::Combine(
				FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
#else
			FString UE_Python = FPaths::Combine(
				FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3"));
#endif
			if (!FPaths::FileExists(UE_Python))
			{
				UE_Python = TEXT("/usr/bin/python3");
			}

			FString ConvLog;
			const bool bConverted = TryConvertSpzToPly(UE_Python, ScriptPath, ResolvedPlyPath, PlyOutPath, ConvLog);

			if (bConverted)
			{
				UE_LOG(LogTemp, Log,
					TEXT("GaussianSplatImportRunner: SPZ→PLY conversion succeeded.\n%s"), *ConvLog);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("GaussianSplatImportRunner: SPZ→PLY conversion failed.\n%s"), *ConvLog);
				PostImportToast(TEXT("Splat import failed: SPZ→PLY conversion error — check Output Log"), false);
				return;
			}
		}

		ResolvedPlyPath = PlyOutPath;
		PLYFilePath = PlyOutPath;
	}

	if (!FPaths::FileExists(ResolvedPlyPath))
	{
		UE_LOG(LogTemp, Error, TEXT("GaussianSplatImportRunner: PLY not found: %s"), *ResolvedPlyPath);
		PostImportToast(FString::Printf(TEXT("Splat import failed: file not found\n%s"), *ResolvedPlyPath), false);
		return;
	}

	// ---- Step 2: Determine spawn position from greybox bounds (Fix 5) ----
	FVector SpawnPos = FVector::ZeroVector;
	{
		FBox GreyboxBounds(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((*It)->Tags.Contains(TEXT("GreyboxVPSplatBuild")))
			{
				const FBox B = (*It)->GetComponentsBoundingBox(true);
				if (B.IsValid)
				{
					GreyboxBounds += B;
				}
			}
		}
		if (GreyboxBounds.IsValid)
		{
			SpawnPos = GreyboxBounds.GetCenter();
		}
		else if (!bUseThisActorTransform)
		{
			SpawnPos = SpawnLocation;
		}
		else
		{
			SpawnPos = GetActorLocation();
		}
	}

	// ---- Step 3: Guard — require SM5 and idle shader compiler (DX12 warmup) ----
	if (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		UE_LOG(LogTemp, Error,
			TEXT("GaussianSplatImportRunner: MLSLabsRenderer expects SM5+ (DX12). "
			     "Current feature level: %d"), static_cast<int32>(GMaxRHIFeatureLevel));
		PostImportToast(TEXT("Gaussian splat requires SM5 / DX12 — splat not placed"), false);
		return;
	}

	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GaussianSplatImportRunner: Shaders are still compiling. "
			     "Retry ImportPLYIntoLevel after the 'Compiling Shaders' task completes."));
		PostImportToast(
			TEXT("Shaders still compiling — re-run Import Splat when complete"), false);
		return;
	}

	// ---- Step 4: Find or create "WorldLabs_Splat" AGaussianSplattingActor (Win64) ----
	FString Err;
	AActor* SplatActor = UGaussianSplatImportHelper::SpawnOrReloadWorldLabsSplatInWorld(
		World, ResolvedPlyPath, SplatScale, SpawnPos, Err);

	LastSpawnedSplat = SplatActor;

	if (!SplatActor)
	{
		const FString Msg = Err.IsEmpty()
			? TEXT("Splat import failed — check Output Log")
			: FString::Printf(TEXT("Splat import failed: %s"), *Err);
		PostImportToast(Msg, false);
		return;
	}

	// ---- Step 5: Redraw all viewports so the splat appears immediately ----
	if (GEditor)
	{
		GEditor->RedrawAllViewports(true);
	}

	// ---- Step 6: Success toast ----
	PostImportToast(TEXT("Splat imported into level"), true);
}

void AGaussianSplatImportRunner::PostImportToast(const FString& Message, bool bSuccess)
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 4.0f;
	Info.bUseLargeFont = false;
	if (bSuccess)
	{
		Info.bUseSuccessFailIcons = true;
	}
	FSlateNotificationManager::Get().AddNotification(Info);
}
