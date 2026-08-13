#include "TramSlotPreviewCamera.h"
#include "TramViewRig.h"
#include "TramDisplayConfiguration.h"
#include "Camera/CameraComponent.h"
#include "Engine/EngineTypes.h"

ATramSlotPreviewCamera::ATramSlotPreviewCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
}

void ATramSlotPreviewCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!ViewRig)
	{
		return;
	}

	const FTransform ObserverTransform = ViewRig->GetSharedObserverTransform();

	float YawOffsetDegrees = 0.f;
	if (DisplayConfiguration && SlotIndex != INDEX_NONE)
	{
		YawOffsetDegrees = DisplayConfiguration->GetSlotCenterAngularOffsetDegrees(SlotIndex);
	}

	// Applied in the observer's own local space (post-multiplication) - "turn further from
	// wherever the shared observer is currently facing", matching how UTramDisplayConfiguration
	// positions screens relative to the rig's own local forward.
	const FQuat FinalRotation = (ObserverTransform.GetRotation() * FRotator(0.f, YawOffsetDegrees, 0.f).Quaternion()).GetNormalized();

	SetActorLocationAndRotation(ObserverTransform.GetLocation(), FinalRotation, false, nullptr, ETeleportType::TeleportPhysics);
}
