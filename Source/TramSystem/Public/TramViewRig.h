// The single shared virtual observer (Objective 10). Every rider derives its final view from
// this actor's evaluation, never from a per-machine independently-computed camera - see
// "One Distributed View Rig" in the objectives.
//
// Pipeline (Objective 10), and the transform-composition order this class assumes:
//   Tram Transform -> Smoothed Observer Base Orientation -> Shared Look Rotation -> (Phase 5+:
//   Physical Screen Projection)
// Concretely: ObserverLocation = TramTransform.TransformPosition(ObserverOffset.Location) -
// the observer rides rigidly with the tram body, unaffected by rotation smoothing. Final
// rotation = SmoothedBaseRotation * ObserverOffset.Rotation * SharedLookRotation - smoothing
// and the fixed mounting offset apply first, the operator's shared look is layered on top.
//
// Rotation-follow smoothing (Objective 11) is a pure fixed time delay applied to an already
// deterministic function: Smoothed(t) = RawTramHeading(t - RotationFollowLagSeconds). Because
// RawTramHeading is itself already a deterministic function of shared state (route + the
// replicated tram anchor + synchronized time - see UTramMovementComponent), this needs no
// extra replicated state or local per-client filtering: every machine samples the exact same
// (deterministic) past heading. On straight track the delay is invisible (heading isn't
// changing); through a turn, the camera visibly lags and eases back once heading stabilizes.
//
// Shared operator look rotation (Objective 12) reuses the same "anchor + deterministic
// evaluation" pattern used for tram motion, applied to rotation: the server periodically
// publishes a Slerp transition (FTramLookRotationState) as the operator's mouse input moves a
// server-local target; every machine evaluates the same Slerp at the same synchronized time.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TramTypes.h"
#include "TramViewRig.generated.h"

class UTramMovementComponent;

UCLASS(BlueprintType)
class TRAMSYSTEM_API ATramViewRig : public AActor
{
	GENERATED_BODY()

public:
	ATramViewRig();

	// The tram Actor this rig observes. Must have a UTramMovementComponent.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|ViewRig")
	TObjectPtr<AActor> TramActor;

	// Observer position/orientation relative to the tram, in the tram's local space. Default
	// approximates a seated eye height inside the cabin; the physical 15ft-circle center
	// offset itself is Phase 5's concern (UTramDisplayConfiguration), not this rig's.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|ViewRig")
	FTransform ObserverOffset = FTransform(FVector(0.f, 0.f, 150.f));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|ViewRig")
	bool bEnableRotationFollowSmoothing = true;

	// How far behind the tram's actual heading the observer's base orientation lags while
	// turning. 0 disables the lag even if bEnableRotationFollowSmoothing is true.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|ViewRig", meta = (ClampMin = "0.0", EditCondition = "bEnableRotationFollowSmoothing"))
	float RotationFollowLagSeconds = 0.4f;

	// --- Shared operator look rotation (Objective 12) ---

	// Replicated (not just server-local) purely for diagnostic visibility - clients never need
	// it for correctness, since it only gates how NEW operator input is accumulated on the
	// server; its effect is already fully captured by the published LookRotationState.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Tram|Look")
	ETramLookAxisMode LookAxisMode = ETramLookAxisMode::YawOnly;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Look", meta = (ClampMin = "0.0"))
	float MouseYawSensitivity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Look", meta = (ClampMin = "0.0"))
	float MousePitchSensitivity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Look", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxPitchDegrees = 80.f;

	// How often the server publishes a new look-rotation transition. Also the transition's
	// duration, so consecutive transitions chain smoothly (each one continues from wherever
	// evaluation of the previous one currently is, avoiding pops between updates).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Look", meta = (ClampMin = "0.02"))
	float LookPublishIntervalSeconds = 0.075f;

	// Authority-only entry point for operator mouse input. Only ever meaningfully called on
	// the listen-server's own process (wiring an actual input binding to it is a host-project
	// concern, same as LaunchTram's HUD button) - HasControlAuthority() alone is therefore
	// sufficient, no Server RPC needed (see UTramMovementComponent's commands for the same
	// reasoning). Deltas for axes not enabled by LookAxisMode are ignored, not reset to zero.
	UFUNCTION(BlueprintCallable, Category = "Tram|Look")
	void ApplyOperatorLookInput(float YawDelta, float PitchDelta, float RollDelta);

	UFUNCTION(BlueprintCallable, Category = "Tram|Look")
	void SetLookAxisMode(ETramLookAxisMode NewMode);

	// --- Queries: identical evaluation on every machine, server included (Objective 24) ---

	UFUNCTION(BlueprintCallable, Category = "Tram|ViewRig")
	FTransform GetSharedObserverTransform() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|ViewRig")
	FQuat GetSmoothedObserverBaseRotation() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Look")
	FQuat GetSharedLookRotation() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	FString GetDiagnosticSummary() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(Replicated)
	FTramLookRotationState LookRotationState;

	UPROPERTY()
	TObjectPtr<UTramMovementComponent> CachedTramMovementComponent;

	// Server-only accumulated operator input target; only the resulting Slerp transitions
	// (LookRotationState) are replicated, not this raw accumulator.
	FRotator DesiredLookRotator = FRotator::ZeroRotator;
	double TimeSinceLastLookPublish = 0.0;

	bool HasControlAuthority() const;
	double GetSynchronizedServerTimeSeconds() const;
	UTramMovementComponent* ResolveTramMovementComponent() const;

	FQuat EvaluateLookRotation(const FTramLookRotationState& State, double Now) const;
	void PublishLookTransition(const FQuat& NewTarget, double Now);
};
