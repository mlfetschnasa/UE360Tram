// Per-connection entry point for rider-facing requests. Unlike the tram Actor (level-placed,
// no owning connection), a PlayerController legitimately owns a connection, so this is the
// correct place for a Server RPC - see Objective networking rules ("prefer Unreal-native
// ownership patterns rather than workaround RPC routing").
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TramPlayerController.generated.h"

UCLASS()
class TRAMSYSTEM_API ATramPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Requests a tram slot. DesiredSlot == INDEX_NONE requests automatic assignment.
	// Safe to call from Blueprint (e.g. an in-game slot-selection UI) at any time, not just
	// at connect time - the server treats every call as a (re-)request.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slot")
	void RequestTramSlot(int32 DesiredSlot);

	// This machine's currently assigned slot, or INDEX_NONE if not yet assigned.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slot")
	int32 GetAssignedTramSlot() const;

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerRequestTramSlot(int32 DesiredSlot);

private:
	// Resolves the slot to request at connect time, in priority order: command line
	// (-TramSlot=N), then a [TramSystem] PreferredSlot= config value, else INDEX_NONE (auto).
	int32 ResolveInitialDesiredSlot() const;
};
