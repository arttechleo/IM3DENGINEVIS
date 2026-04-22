// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualProductionSplatEditor.h"
#include "Modules/ModuleManager.h"
#include "VPToolbarExtension.h"

class FVirtualProductionSplatEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FVirtualProductionSplatEditorModule, VirtualProductionSplatEditor);

void FVirtualProductionSplatEditorModule::StartupModule()
{
	FVPToolbarExtension::Register();
}

void FVirtualProductionSplatEditorModule::ShutdownModule()
{
	FVPToolbarExtension::Unregister();
}
