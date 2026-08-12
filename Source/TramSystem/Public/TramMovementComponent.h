// Makes its owning Actor behave as a spline-driven tram. Reusable on any Actor class.
//
// Design: rather than integrating DeltaTime every tick, this component holds a single
// authoritative "anchor" snapshot (FTramMotionState) and re-evaluates current distance/speed
// as a deterministic closed-form function of (anchor, SynchronizedServerTime - anchor time)
// every tick. LaunchTram/PauseTram/ResumeTram/StopTram simply publish a fresh anchor.
//
// Networking (Phase 2): CurrentSnapshot is a ReplicatedUsing property - the server is the only
// writer (via PublishSnapshot), and every other machine receives it through OnRep_CurrentSnapshot.
// No Server RPCs are used for the command functions: Launch/Pause/Resume/Stop are only ever
// invoked locally by the listen-server operator's own process, so HasControlAuthority() alone
// is sufficient and there is no "RPC from an object without an owning connection" concern.
// The owning Actor must have bReplicates = true for any of this to take effect; the component
// also forces bAlwaysRelevant = true on its owner in BeginPlay, since the tram is the one
// shared object every rider must always see regardless of UE's distance-based relevancy culling.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TramTypes.h"
#include "TramMovementComponent.generated.h"

class ATramSplineRoute;

UCLASS(ClassGroup = (Tram), meta = (BlueprintSpawnableComponent))
class TRAMSYSTEM_API UTramMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTramMovementComponent();

	// --- Configuration ---

	// The route this tram follows. Expected to be placed in the same level (route data is
	// static, authored per-level, and does not need to be networked - see ATramSplineRoute).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|Route")
	TObjectPtr<ATramSplineRoute> Route;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Motion", meta = (ClampMin = "0.0"))
	float AccelerationCmss = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Motion", meta = (ClampMin = "0.0"))
	float DecelerationCmss = 200.f;

	// 0 = unlimited (segment target speeds are trusted as configured).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Motion", meta = (ClampMin = "0.0"))
	float MaxSpeedCms = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Motion", meta = (ClampMin = "0.0"))
	float StartingDistanceCms = 0.f;

	// How often the authority re-anchors (re-publishes) its snapshot while Running. This
	// bounds extrapolation error accumulation and, from Phase 2 onward, is also the network
	// publish cadence.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Network", meta = (ClampMin = "0.05"))
	float SnapshotPublishIntervalSeconds = 0.5f;

	// Prediction error at/below this magnitude is ignored - the new anchor is adopted directly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Network", meta = (ClampMin = "0.0"))
	float CorrectionSnapThresholdCm = 3.f;

	// Prediction error above this magnitude snaps instantly (with a warning) instead of
	// blending, since a smooth correction over such a large distance would itself look wrong.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Network", meta = (ClampMin = "0.0"))
	float CorrectionSevereThresholdCm = 300.f;

	// Duration of the client-local blend from the old predicted trajectory to the new
	// authoritative one, for errors between the two thresholds above.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Network", meta = (ClampMin = "0.0"))
	float CorrectionBlendDurationSeconds = 0.35f;

	// Upper bound on how far a snapshot is extrapolated into the future. Guards against wild
	// extrapolation if replication stalls or synchronized time jumps unexpectedly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Network", meta = (ClampMin = "0.1"))
	float MaxExtrapolationSeconds = 3.f;

	// --- Authority-only commands (server-only from Phase 2 onward) ---

	UFUNCTION(BlueprintCallable, Category = "Tram|Commands")
	void LaunchTram();

	UFUNCTION(BlueprintCallable, Category = "Tram|Commands")
	void PauseTram();

	UFUNCTION(BlueprintCallable, Category = "Tram|Commands")
	void ResumeTram();

	UFUNCTION(BlueprintCallable, Category = "Tram|Commands")
	void StopTram();

	// --- Queries (safe on any machine; reflect this machine's most recent evaluation) ---

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	ETramMovementState GetMovementState() const { return CurrentSnapshot.MovementState; }

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	double GetDistanceAlongSplineCm() const { return LastEvaluatedDistanceCm; }

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	double GetCurrentSpeedCms() const { return LastEvaluatedSpeedCms; }

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	int32 GetCurrentSegmentIndex() const { return LastEvaluatedSegmentIndex; }

	// Deterministic authoritative transform (route position+rotation) at an arbitrary
	// synchronized server time - past, present, or a little into the future - ignoring this
	// machine's own transient correction blend (if any is active). Every machine holding the
	// same anchor computes an identical result for the same QueryServerTime, which is exactly
	// what systems like ATramViewRig's rotation-follow smoothing need: they sample this at a
	// time offset from "now" and must agree with every other machine on the result.
	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	FTransform GetAuthoritativeTransformAtServerTime(double QueryServerTime) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	FTramMotionState GetAuthoritativeSnapshot() const { return CurrentSnapshot; }

	// Synchronized server-world time. Backed by AGameStateBase::GetServerWorldTimeSeconds(),
	// which already compensates for client/server clock offset over the network connection
	// and degrades to plain world time in standalone play - see class comment in .cpp.
	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	double GetSynchronizedServerTimeSeconds() const;

	// Signed distance error (cm) observed at the most recent correction (0 if none yet).
	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	double GetLastPredictionErrorCm() const { return LastPredictionErrorCm; }

	// Signed distance correction (cm) applied at the most recent correction (0 if none yet).
	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	double GetLastCorrectionAmountCm() const { return LastCorrectionAmountCm; }

	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	bool IsCorrectionBlendActive() const { return bCorrectionBlendActive; }

	// One-line human-readable snapshot of this machine's tram diagnostics, for HUD/log use.
	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	FString GetDiagnosticSummary() const;

	//~ Begin UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent interface

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Fires on every non-authority machine when a new authoritative snapshot arrives. This is
	// where prediction-error measurement and correction (blend or snap) happen; the authority
	// never receives this callback for its own writes.
	UFUNCTION()
	void OnRep_CurrentSnapshot(FTramMotionState OldSnapshot);

private:
	UPROPERTY(ReplicatedUsing = OnRep_CurrentSnapshot)
	FTramMotionState CurrentSnapshot;

	double LastEvaluatedDistanceCm = 0.0;
	double LastEvaluatedSpeedCms = 0.0;
	int32 LastEvaluatedSegmentIndex = 0;

	double TimeSinceLastPublish = 0.0;

	// --- Correction/blend state (client-side only; see OnRep_CurrentSnapshot) ---
	bool bHasReceivedAnySnapshot = false;
	bool bCorrectionBlendActive = false;
	FTramMotionState PreviousSnapshotForBlend;
	double CorrectionBlendStartServerTime = 0.0;
	double LastPredictionErrorCm = 0.0;
	double LastCorrectionAmountCm = 0.0;
	mutable double LastStaleSnapshotWarningTime = -1000.0;

	bool HasControlAuthority() const;

	// Evaluates CurrentSnapshot at QueryServerTime (applying the active correction blend, if
	// any), caches the result, and applies the resulting transform to the owning Actor.
	void EvaluateAndApply(double QueryServerTime);

	// Pure evaluation, no side effects: walks forward (possibly across multiple segments)
	// from Snapshot by ElapsedSeconds using deterministic rate-based kinematics.
	void Evaluate(const FTramMotionState& Snapshot, double ElapsedSeconds, double& OutDistanceCm, double& OutSpeedCms, int32& OutSegmentIndex) const;

	// Evaluate() with the (Now - Snapshot.ServerTimestamp) elapsed time clamped to
	// MaxExtrapolationSeconds and never negative; logs a throttled warning if clamping occurred.
	void EvaluateClamped(const FTramMotionState& Snapshot, double QueryServerTime, double& OutDistanceCm, double& OutSpeedCms, int32& OutSegmentIndex) const;

	// Assigns CurrentSnapshot and resets the publish timer. The single point through which the
	// authority's state changes flow (and, via replication, propagate to every other machine).
	void PublishSnapshot(const FTramMotionState& NewSnapshot);
};
