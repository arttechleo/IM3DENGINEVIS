// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLSLabsEditorVerification.h"
#include "MLSGaussianSplatInterop.h"
#include "VirtualProductionSplat.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

static int32 CountDllsInDirectory(const FString& Dir)
{
	if (!FPaths::DirectoryExists(Dir))
	{
		return -1;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.dll")), true, false);
	return Files.Num();
}

void FMLSLabsEditorVerification::LogFullReport()
{
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString PluginDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(ProjectDir, TEXT("Plugins/MLSLabsRenderer")));
	const FString UPlugin = FPaths::Combine(PluginDir, TEXT("MLSLabsRenderer.uplugin"));
	const FString BuildCs = FPaths::Combine(PluginDir, TEXT("Source/MLSLabsRenderer/MLSLabsRenderer.Build.cs"));
	const FString LtRoot = FPaths::Combine(PluginDir, TEXT("libtorch"));
	const FString LtInc = FPaths::Combine(LtRoot, TEXT("include"));
	const FString LtLib = FPaths::Combine(LtRoot, TEXT("lib"));
	const FString LtBin = FPaths::Combine(LtRoot, TEXT("bin"));
	const FString PlugBin = FPaths::Combine(PluginDir, TEXT("Binaries/Win64"));
	const FString ProjBin = FPaths::Combine(ProjectDir, TEXT("Binaries/Win64"));

	UE_LOG(LogVPSplat, Log, TEXT("=== Verify MLSLabs (editor) ==="));
	UE_LOG(LogVPSplat, Log, TEXT("ProjectDir: %s"), *ProjectDir);

	const bool bUProject = FPaths::FileExists(FPaths::Combine(ProjectDir, TEXT("VirtualProductionSplat.uproject")));
	UE_LOG(LogVPSplat, Log, TEXT(".uproject present: %s"), bUProject ? TEXT("YES") : TEXT("NO"));

	TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("MLSLabsRenderer"));
	UE_LOG(LogVPSplat, Log, TEXT("IPluginManager MLSLabsRenderer: %s (enabled=%s)"),
		P.IsValid() ? TEXT("found") : TEXT("MISSING"),
		(P.IsValid() && P->IsEnabled()) ? TEXT("yes") : TEXT("no"));

	UE_LOG(LogVPSplat, Log, TEXT("Plugin dir exists: %s | path=%s"),
		FPaths::DirectoryExists(PluginDir) ? TEXT("YES") : TEXT("NO"), *PluginDir);
	UE_LOG(LogVPSplat, Log, TEXT(".uplugin exists: %s"), FPaths::FileExists(UPlugin) ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogVPSplat, Log, TEXT("Build.cs exists: %s"), FPaths::FileExists(BuildCs) ? TEXT("YES") : TEXT("NO"));

	UE_LOG(LogVPSplat, Log, TEXT("libtorch root exists: %s"), FPaths::DirectoryExists(LtRoot) ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogVPSplat, Log, TEXT("libtorch/include: %s"), FPaths::DirectoryExists(LtInc) ? TEXT("YES") : TEXT("NO"));
	const int32 LibDlls = CountDllsInDirectory(LtLib);
	const int32 BinDlls = CountDllsInDirectory(LtBin);
	UE_LOG(LogVPSplat, Log, TEXT("libtorch/lib DLL count: %d (missing dir = -1)"), LibDlls);
	UE_LOG(LogVPSplat, Log, TEXT("libtorch/bin DLL count: %d (missing dir = -1)"), BinDlls);

	const FString TorchCudaLt = FPaths::Combine(LtLib, TEXT("torch_cuda.dll"));
	UE_LOG(LogVPSplat, Log, TEXT("torch_cuda.dll under libtorch/lib: %s"), FPaths::FileExists(TorchCudaLt) ? TEXT("YES") : TEXT("NO"));

	const int32 PlugBinDlls = CountDllsInDirectory(PlugBin);
	UE_LOG(LogVPSplat, Log, TEXT("Plugin Binaries/Win64 DLL count: %d"), PlugBinDlls);

	const FString TorchCudaProj = FPaths::Combine(ProjBin, TEXT("torch_cuda.dll"));
	UE_LOG(LogVPSplat, Log, TEXT("Project Binaries/Win64 torch_cuda.dll: %s"), FPaths::FileExists(TorchCudaProj) ? TEXT("YES") : TEXT("NO"));

	FString ModErr;
	const bool bMod = FMLSGaussianSplatInterop::EnsureMLSLabsRendererReady(ModErr);
	UE_LOG(LogVPSplat, Log, TEXT("FModuleManager MLSLabsRenderer ready: %s"), bMod ? TEXT("YES") : TEXT("NO"));
	if (!bMod)
	{
		UE_LOG(LogVPSplat, Error, TEXT("Module load detail: %s"), *ModErr);
	}

	UE_LOG(LogVPSplat, Log, TEXT("=== End Verify MLSLabs ==="));
}
