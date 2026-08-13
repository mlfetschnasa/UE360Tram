// Module entry point and shared logging category for the TramSystemDisplayCluster module. See
// TramDisplayClusterViewSync.h for the actual nDisplay integration and why it lives here,
// isolated from the core TramSystem module.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

TRAMSYSTEMDISPLAYCLUSTER_API DECLARE_LOG_CATEGORY_EXTERN(LogTramSystemDisplayCluster, Log, All);

class FTramSystemDisplayClusterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
