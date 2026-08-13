// Server-only slot assignment authority. Deliberately small: this class owns exactly one
// decision (which slot, if any, a given rider ends up with) plus the class-default wiring
// that makes a project's GameMode use the Tram player state/controller/game state classes.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TramGameMode.generated.h"

class APlayerState;

UCLASS()
class TRAMSYSTEM_API ATramGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATramGameMode();

	// If true, a request for a specific slot that is already occupied is rejected outright
	// (the rider is left unassigned) instead of silently falling back to another free slot.
	// Default favors automatic placement per Objective 8.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tram|Slots")
	bool bStrictSlotRequests = false;

	// Server-authoritative slot assignment. DesiredSlot == INDEX_NONE requests automatic
	// assignment. Returns true if ForPlayer ended up with a slot. Never leaves authoritative
	// state (the slot registry) in a corrupt/inconsistent form, even on failure.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slots")
	bool TryAssignTramSlot(APlayerState* ForPlayer, int32 DesiredSlot);

	virtual void Logout(AController* Exiting) override;
};
