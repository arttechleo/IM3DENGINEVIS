// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FVPToolbarExtension
{
public:
	static void Register();
	static void Unregister();

private:
	static void OnSetupVPLevel();
	static void OnCapturePanorama();
	static void OnSubmitToWorldLabs();
	static void OnImportLatestSplat();
	static void OnOpenWorldPreview();

	static FDelegateHandle MenuStartupHandle;
};
