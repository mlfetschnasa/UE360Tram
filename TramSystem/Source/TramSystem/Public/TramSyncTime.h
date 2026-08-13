// Shared accessor for synchronized server time, used by every subsystem that must evaluate
// state deterministically from (shared state, synchronized time) - see UTramMovementComponent
// and ATramViewRig.
#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace TramSyncTime
{
	// Backed by AGameStateBase::GetServerWorldTimeSeconds(), Unreal's built-in mechanism for
	// client/server clock-offset compensation, rather than a bespoke NTP-style exchange (see
	// Rule 14: prefer built-in Unreal systems over duplicating them). Degrades to plain world
	// time when there is no GameState yet (e.g. very early in level load).
	TRAMSYSTEM_API double GetSynchronizedServerTimeSeconds(const UWorld* World);
}
