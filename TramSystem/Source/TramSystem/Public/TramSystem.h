// Module entry point and shared logging category for the TramSystem plugin.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

TRAMSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogTramSystem, Log, All);

class FTramSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
