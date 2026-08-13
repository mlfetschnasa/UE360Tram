// Simplified single-camera-per-machine dev/test mode (Objective 27): NOT the production
// per-screen off-axis projection (that's Phase 6). This is a purely local, non-replicated
// convenience actor - one is spawned per connecting machine by ATramPlayerController, never
// shared or synchronized itself - that reads the same shared observer transform every other
// machine reads and adds a yaw offset toward its own assigned slot's portion of the circle,
// so different machines can preview different slices of the installation instead of all
// showing the identical view. It owns no simulation state; every input it reads (the observer
// transform, the display configuration's screen geometry) is already shared/deterministic.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TramSlotPreviewCamera.generated.h"

class ATramViewRig;
class UTramDisplayConfiguration;
class UCameraComponent;

UCLASS()
class TRAMSYSTEM_API ATramSlotPreviewCamera : public AActor
{
	GENERATED_BODY()

public:
	ATramSlotPreviewCamera();

	UPROPERTY(BlueprintReadWrite, Category = "Tram|Debug")
	TObjectPtr<ATramViewRig> ViewRig;

	UPROPERTY(BlueprintReadWrite, Category = "Tram|Debug")
	TObjectPtr<UTramDisplayConfiguration> DisplayConfiguration;

	// INDEX_NONE = no slot assigned yet; camera then just shows the un-offset shared view.
	UPROPERTY(BlueprintReadWrite, Category = "Tram|Debug")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tram|Debug")
	TObjectPtr<UCameraComponent> Camera;

protected:
	virtual void Tick(float DeltaTime) override;
};
