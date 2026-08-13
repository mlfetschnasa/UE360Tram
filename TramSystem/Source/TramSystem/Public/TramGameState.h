// Replicated shared slot-registry configuration and read-only occupancy queries. Occupancy
// is derived live from PlayerArray (each ATramPlayerState's SlotIndex) rather than kept in a
// second, separately-maintained array, so there is nothing to desync.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TramGameState.generated.h"

UCLASS()
class TRAMSYSTEM_API ATramGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// Number of rider/view slots available in this installation. Independent of how many
	// physical displays each slot drives (see Phase 5's UTramDisplayConfiguration) and
	// independent of how many machines happen to be connected right now. Default of 4 matches
	// the reference installation but is not assumed anywhere else in the plugin.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Tram|Slots", meta = (ClampMin = "1"))
	int32 NumTramSlots = 4;

	UFUNCTION(BlueprintCallable, Category = "Tram|Slots")
	bool IsSlotOccupied(int32 SlotIndex) const;

	// Returns INDEX_NONE if every slot in [0, NumTramSlots) is occupied.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slots")
	int32 FindFirstFreeSlot() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Slots")
	TArray<int32> GetOccupiedSlots() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
