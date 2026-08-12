#include "TramSyncTime.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

double TramSyncTime::GetSynchronizedServerTimeSeconds(const UWorld* World)
{
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return World->GetTimeSeconds();
}
