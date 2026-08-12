#include "TramDisplayDebugComponent.h"
#include "TramViewRig.h"
#include "TramDisplayConfiguration.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

UTramDisplayDebugComponent::UTramDisplayDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTramDisplayDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDrawDebugVisualization || !DisplayConfiguration || !ViewRig)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FTransform ObserverTransform = ViewRig->GetSharedObserverTransform();
	const FVector ObserverLocation = ObserverTransform.GetLocation();

	// The 15ft (default) virtual display circle, drawn flat in the world XY plane.
	DrawDebugCircle(World, ObserverLocation, DisplayConfiguration->CircleRadiusCm, 64, FColor::Cyan,
		false, -1.f, 0, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);

	DrawDebugSphere(World, ObserverLocation, 10.f, 12, FColor::Yellow, false, -1.f, 0, 1.f);

	for (const FTramScreenDefinition& Screen : DisplayConfiguration->Screens)
	{
		const FTransform ScreenLocalTransform = DisplayConfiguration->GetScreenLocalTransform(Screen.DisplayIndex);
		const FTransform ScreenWorldTransform = ScreenLocalTransform * ObserverTransform;

		const FVector ScreenLocation = ScreenWorldTransform.GetLocation();
		const FVector ScreenForward = ScreenWorldTransform.GetRotation().RotateVector(FVector::ForwardVector);
		const int32 OwningSlot = DisplayConfiguration->GetSlotForDisplay(Screen.DisplayIndex);

		DrawDebugBox(World, ScreenLocation,
			FVector(2.f, DisplayConfiguration->DisplayWidthCm * 0.5f, DisplayConfiguration->DisplayHeightCm * 0.5f),
			ScreenWorldTransform.GetRotation(), FColor::Green, false, -1.f, 0, 1.5f);

		DrawDebugDirectionalArrow(World, ScreenLocation, ScreenLocation + ScreenForward * 60.f, 20.f, FColor::Red, false, -1.f, 0, 2.f);

		const FString Label = (OwningSlot != INDEX_NONE)
			? FString::Printf(TEXT("%d (Slot %d)"), Screen.DisplayIndex, OwningSlot)
			: FString::Printf(TEXT("%d (unassigned)"), Screen.DisplayIndex);
		DrawDebugString(World, ScreenLocation, Label, nullptr, FColor::White, 0.f, true, 1.f);
	}

	// Composed observer forward (smoothed tram heading + shared look) at the rig origin, for
	// comparing against individual screen forward vectors above.
	const FVector ObserverForward = ObserverTransform.GetRotation().RotateVector(FVector::ForwardVector);
	DrawDebugDirectionalArrow(World, ObserverLocation, ObserverLocation + ObserverForward * 150.f, 25.f, FColor::Orange, false, -1.f, 0, 3.f);
}
