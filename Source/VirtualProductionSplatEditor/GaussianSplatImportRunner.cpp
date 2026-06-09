// Copyright Epic Games, Inc. All Rights Reserved.

#include "GaussianSplatImportRunner.h"
#include "GaussianSplatImportHelper.h"
#include "MLSGaussianSplatInterop.h"
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
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

// NanoGS plugin API
#include "GaussianSplatAsset.h"

#if WITH_EDITOR
#include "GaussianSplatAssetFactory.h"   // NanoGSEditor: imports .ply -> UGaussianSplatAsset
#endif

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

#if WITH_EDITOR
/**
 * Import a .ply into a persistent UGaussianSplatAsset under /Game/GaussianSplats via the
 * NanoGS editor factory, so it shows up in the Content Browser. Returns nullptr (OutError set) on failure.
 */
static UGaussianSplatAsset* ImportPlyViaFactory(const FString& PlyPath, FString& OutError)
{
	const FString BaseName = FPaths::GetBaseFilename(PlyPath);
	const FString PackageName = FString::Printf(TEXT("/Game/GaussianSplats/%s"), *BaseName);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = TEXT("CreatePackage failed for /Game/GaussianSplats.");
		return nullptr;
	}
	Package->FullyLoad();

	UGaussianSplatAssetFactory* Factory = NewObject<UGaussianSplatAssetFactory>();
	bool bCanceled = false;
	UObject* Created = Factory->FactoryCreateFile(
		UGaussianSplatAsset::StaticClass(), Package, FName(*BaseName),
		RF_Public | RF_Standalone, PlyPath, nullptr, GWarn, bCanceled);

	UGaussianSplatAsset* Asset = Cast<UGaussianSplatAsset>(Created);
	if (!Asset)
	{
		OutError = bCanceled ? TEXT("PLY import canceled.") : TEXT("UGaussianSplatAssetFactory returned no asset.");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	return Asset;
}
#endif // WITH_EDITOR

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
					TEXT("GaussianSplatImportRunner: SPZ->PLY conversion FAILED.\n%s"), *ConvLog);

				FNotificationInfo Info(FText::FromString(
					TEXT("SPZ→PLY conversion failed — check Output Log")));
				Info.bFireAndForget = true;
				Info.ExpireDuration = 6.0f;
				FSlateNotificationManager::Get().AddNotification(Info);
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

	const int64 PlyBytes = IFileManager::Get().FileSize(*ResolvedPlyPath);
	if (PlyBytes <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("GaussianSplatImportRunner: PLY missing or empty (size=%lld): %s"),
			static_cast<long long>(PlyBytes), *ResolvedPlyPath);
		PostImportToast(TEXT("Splat import failed: PLY file is empty or unreadable"), false);
		return;
	}

	const FString PlyDir = FPaths::GetPath(ResolvedPlyPath);
	if (!FPaths::DirectoryExists(PlyDir))
	{
		UE_LOG(LogTemp, Error, TEXT("GaussianSplatImportRunner: PLY parent directory missing: %s"), *PlyDir);
		PostImportToast(TEXT("Splat import failed: invalid PLY path"), false);
		return;
	}

	// Guard 1: minimum sensible PLY size (reuse PlyBytes already computed above)
	if (PlyBytes < 1024)
	{
		UE_LOG(LogTemp, Error,
			TEXT("ImportPLY: file too small (%lld bytes), likely corrupt: %s"),
			PlyBytes, *ResolvedPlyPath);
		PostImportToast(TEXT("Import failed — PLY file corrupt or empty"), false);
		return;
	}

	// Guard 2: verify PLY magic bytes
	{
		uint8 MagicBytes[4] = { 0, 0, 0, 0 };
		if (FArchive* Ar = IFileManager::Get().CreateFileReader(*ResolvedPlyPath))
		{
			Ar->Serialize(MagicBytes, 3);
			delete Ar;
		}
		if (MagicBytes[0] != 'p' || MagicBytes[1] != 'l' || MagicBytes[2] != 'y')
		{
			UE_LOG(LogTemp, Error,
				TEXT("ImportPLY: not a valid PLY file (bad magic): %s"),
				*ResolvedPlyPath);
			PostImportToast(TEXT("Import failed — not a valid PLY"), false);
			return;
		}
	}

	// Guard 3: all guards passed
	UE_LOG(LogTemp, Warning,
		TEXT("ImportPLY: guards passed. Size=%lld bytes. Building NanoGS asset."),
		PlyBytes);

	FString ModuleErr;
	if (!FMLSGaussianSplatInterop::EnsureMLSLabsRendererReady(ModuleErr))
	{
		UE_LOG(LogTemp, Error, TEXT("GaussianSplatImportRunner: NanoGS not ready: %s"), *ModuleErr);
		PostImportToast(FString::Printf(TEXT("Splat import aborted: %s"), *ModuleErr), false);
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
			TEXT("GaussianSplatImportRunner: NanoGS expects SM5+ (DX12). "
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

	// ---- Step 4: Build the Gaussian Splat asset ----
	// Editor: import as a persistent UGaussianSplatAsset via the NanoGS factory (Content Browser).
	// Runtime / fallback: parse the .ply into a transient asset (FPLYFileReader + InitializeFromSplatData).
	FString Err;
	UGaussianSplatAsset* SplatAsset = nullptr;

#if WITH_EDITOR
	SplatAsset = ImportPlyViaFactory(ResolvedPlyPath, Err);
	if (!SplatAsset)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GaussianSplatImportRunner: factory import failed (%s) — falling back to transient asset."), *Err);
	}
#endif

	if (!SplatAsset)
	{
		SplatAsset = FMLSGaussianSplatInterop::CreateSplatAssetFromPly(ResolvedPlyPath, World, Err);
	}

	if (!SplatAsset)
	{
		const FString Msg = Err.IsEmpty()
			? TEXT("Splat import failed — check Output Log")
			: FString::Printf(TEXT("Splat import failed: %s"), *Err);
		PostImportToast(Msg, false);
		return;
	}

	// ---- Step 5: Spawn / reload the "WorldLabs_Splat" actor with this asset ----
	const FTransform SpawnXform(FRotator::ZeroRotator, SpawnPos, FVector::OneVector);
	AActor* SplatActor = FMLSGaussianSplatInterop::SpawnActorWithAsset(
		World, SplatAsset, SplatScale, SpawnXform, /*bReuseWorldLabsSingleton*/ true, Err);

	LastSpawnedSplat = SplatActor;

	if (!SplatActor)
	{
		const FString Msg = Err.IsEmpty()
			? TEXT("Splat import failed — check Output Log")
			: FString::Printf(TEXT("Splat import failed: %s"), *Err);
		PostImportToast(Msg, false);
		return;
	}

	// ---- Step 6: Scan asset registry so Content Browser reflects the new .ply ----
	FAssetRegistryModule::GetRegistry().ScanPathsSynchronous(
		TArray<FString>{ TEXT("/Game/GaussianSplats") }, true);

	// ---- Step 7: Redraw all viewports so the splat appears immediately ----
	if (GEditor)
	{
		GEditor->RedrawAllViewports(true);
	}

	// ---- Step 8: Success toast ----
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
