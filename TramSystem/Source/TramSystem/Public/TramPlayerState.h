// Per-rider replicated slot assignment. Separate from network/connection identity: a
// "rider" is a connected machine, a "slot" is that machine's assigned portion of the
// physical display circle (see ATramGameState/ATramGameMode for the slot registry and
// assignment algorithm). Lives on PlayerState, not the Controller, because other clients
// need to know everyone's slot - PlayerState is Unreal's native mechanism for exactly that.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TramPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTramSlotChangedSignature, int32, NewSlotIndex);

UCLASS()
class TRAMSYSTEM_API ATramPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	// Broadcast on every machine (server via SetSlotIndex, clients via OnRep) whenever this
	// rider's slot changes - a single Blueprint-bindable hook for HUD/menu updates.
	UPROPERTY(BlueprintAssignable, Category = "Tram|Slot")
	FTramSlotChangedSignature OnTramSlotChanged;

	UFUNCTION(BlueprintCallable, Category = "Tram|Slot")
	int32 GetSlotIndex() const { return SlotIndex; }

	// Server-only. Use ATramGameMode::TryAssignTramSlot rather than calling this directly
	// unless you are implementing slot assignment logic yourself.
	void SetSlotIndex(int32 NewSlotIndex);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_SlotIndex();

private:
	UPROPERTY(ReplicatedUsing = OnRep_SlotIndex)
	int32 SlotIndex = INDEX_NONE;
};
