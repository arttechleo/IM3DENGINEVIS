// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VirtualProductionSplat : ModuleRules
{
	public VirtualProductionSplat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"HTTP",
			"Json",
			"JsonUtilities",
			"ImageWrapper",
			"NanoGS"          // Gaussian Splat renderer plugin (AGaussianSplatActor, UGaussianSplatAsset, FPLYFileReader)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"Projects"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"Slate",
				"SlateCore"
			});
		}
	}
}
