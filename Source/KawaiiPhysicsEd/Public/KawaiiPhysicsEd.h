// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

// The runtime module's LogKawaiiPhysics is not exported across module
// boundaries, so the editor module carries its own category.
DECLARE_LOG_CATEGORY_EXTERN(LogKawaiiPhysicsEd, Log, All);

class FKawaiiPhysicsEdModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
