// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VirtualProductionSplatEditor : ModuleRules
{
	public VirtualProductionSplatEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"VirtualProductionSplat"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"InputCore",
			"ApplicationCore",
			"UnrealEd",
			"Blutility",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"ToolMenus",
			"EditorScriptingUtilities",
			"AssetTools",
			"CinematicCamera",
			"Kismet",
			"PythonScriptPlugin",
			"Json",
			"RHI",              // GMaxRHIFeatureLevel
			"RenderCore",       // GShaderCompilingManager (via ShaderCompiler.h in Engine)
			"DesktopPlatform",  // IDesktopPlatform file open dialog
			"AssetRegistry",    // FAssetRegistryModule::GetRegistry().ScanPathsSynchronous
			"NanoGS",           // UGaussianSplatAsset / AGaussianSplatActor (runtime types used by the runner)
		});

		// Editor-only NanoGS module: UGaussianSplatAssetFactory (.ply import). Guarded so non-editor targets skip it.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("NanoGSEditor");
		}
	}
}
