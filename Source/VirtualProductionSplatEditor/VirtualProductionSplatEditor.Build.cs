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
		});
	}
}
