// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct VIRTUALPRODUCTIONSPLAT_API FLogCategoryLogVPSplat : public FLogCategory<ELogVerbosity::Log, ELogVerbosity::All>
{
	UE_FORCEINLINE_HINT FLogCategoryLogVPSplat() : FLogCategory(TEXT("LogVPSplat")) {}
};
extern VIRTUALPRODUCTIONSPLAT_API FLogCategoryLogVPSplat LogVPSplat;
