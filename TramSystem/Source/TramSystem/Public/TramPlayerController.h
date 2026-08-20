// Per-connection entry point for rider-facing requests. Unlike the tram Actor (level-placed,
// no owning connection), a PlayerController legitimately owns a connection, so this is the
// correct place for a Server RPC - see Objective networking rules ("prefer Unreal-native
// ownership patterns rather than workaround RPC routing").
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TramPlayerState.h"
#include "TramPlayerController.generated.h"

class UTramDisplayConfiguration;
class ATramSlotPreviewCamera;

UCLASS()
class TRAMSYSTEM_API ATramPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// Optional. If set (e.g. in a Blueprint child's Class Defaults - it's a content asset, not
	// a level actor, so unlike Route/TramActor this does NOT need to be EditInstanceOnly),
	// this machine spawns its own local ATramSlotPreviewCamera and views through it instead of
	// the shared ATramViewRig directly, oriented toward its assigned slot's portion of the
	// circle (Objective 27's simplified single-camera-per-machine dev mode). Leave unset to
	// keep the previous "identical shared view on every machine" behavior.
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Tram|Debug")
	TObjectPtr<UTramDisplayConfiguration> DevPreviewDisplayConfiguration;

	// Requests a tram slot. DesiredSlot == INDEX_NONE requests automatic assignment.
	// Safe to call from Blueprint (e.g. an in-game slot-selection UI) at any time, not just
	// at connect time - the server treats every call as a (re-)request.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slot")
	void RequestTramSlot(int32 DesiredSlot);

	// This machine's currently assigned slot, or INDEX_NONE if not yet assigned.
	UFUNCTION(BlueprintCallable, Category = "Tram|Slot")
	int32 GetAssignedTramSlot() const;

	// Mirrors this controller's own PlayerState's OnTramSlotChanged, but is safe to bind to
	// exactly once at BeginPlay regardless of PlayerState replication timing: the controller
	// itself tracks when PlayerState becomes valid (BeginPlay for the locally-authoritative
	// case, OnRep_PlayerState for a remote client where it can arrive after BeginPlay) and
	// (re)binds to it internally, firing this once immediately with the current value at bind
	// time too. Bind to THIS, not to PlayerState's event directly, from Blueprint.
	UPROPERTY(BlueprintAssignable, Category = "Tram|Slot")
	FTramSlotChangedSignature OnLocalTramSlotChanged;

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_PlayerState() override;

	UFUNCTION(Server, Reliable)
	void ServerRequestTramSlot(int32 DesiredSlot);

private:
	// Resolves the slot to request at connect time, in priority order: command line
	// (-TramSlot=N), then nDisplay's own -dc_node= launch arg (its trailing digits, e.g.
	// "Node_2" -> 2 - see .cpp), then a [TramSystem] PreferredSlot= config value, else
	// INDEX_NONE (auto).
	int32 ResolveInitialDesiredSlot() const;

	// (Re)binds to the current PlayerState's OnTramSlotChanged if it has changed since the
	// last call, unbinding from the previous one first. Idempotent - safe to call from both
	// BeginPlay and OnRep_PlayerState without double-binding.
	void BindToPlayerStateSlotChanges();

	UFUNCTION()
	void HandlePlayerStateSlotChanged(int32 NewSlotIndex);

	TWeakObjectPtr<ATramPlayerState> BoundPlayerState;

	UPROPERTY()
	TObjectPtr<ATramSlotPreviewCamera> DevPreviewCamera;
};
