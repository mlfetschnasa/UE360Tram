// Makes its owning Actor behave as a spline-driven tram. Reusable on any Actor class.
//
// Design: rather than integrating DeltaTime every tick, this component holds a single
// authoritative "anchor" snapshot (FTramMotionState) and re-evaluates current distance/speed
// as a deterministic closed-form function of (anchor, SynchronizedServerTime - anchor time)
// every tick. LaunchTram/PauseTram/ResumeTram/StopTram simply publish a fresh anchor.
//
// This makes the component network-ready without redesign: in Phase 1 (this file) there is
// no replication yet, so the "anchor" is just local authoritative state (HasAuthority() is
// always true in standalone play). Phase 2 adds replication to CurrentSnapshot and RPCs for
// the command functions; the evaluation logic itself does not change.
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

	UFUNCTION(BlueprintCallable, Category = "Tram|State")
	FTramMotionState GetAuthoritativeSnapshot() const { return CurrentSnapshot; }

	// Synchronized server-world time. Backed by AGameStateBase::GetServerWorldTimeSeconds(),
	// which already compensates for client/server clock offset over the network connection
	// and degrades to plain world time in standalone play - see class comment in .cpp.
	UFUNCTION(BlueprintCallable, Category = "Tram|Diagnostics")
	double GetSynchronizedServerTimeSeconds() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	FTramMotionState CurrentSnapshot;

	double LastEvaluatedDistanceCm = 0.0;
	double LastEvaluatedSpeedCms = 0.0;
	int32 LastEvaluatedSegmentIndex = 0;

	double TimeSinceLastPublish = 0.0;

	bool HasControlAuthority() const;

	// Evaluates CurrentSnapshot at QueryServerTime, caches the result, and applies the
	// resulting transform to the owning Actor.
	void EvaluateAndApply(double QueryServerTime);

	// Pure evaluation, no side effects: walks forward (possibly across multiple segments)
	// from Snapshot by ElapsedSeconds using deterministic rate-based kinematics.
	void Evaluate(const FTramMotionState& Snapshot, double ElapsedSeconds, double& OutDistanceCm, double& OutSpeedCms, int32& OutSegmentIndex) const;

	// Assigns CurrentSnapshot and resets the publish timer. The single point through which
	// all authoritative state changes flow (Phase 2 hangs replication off this).
	void PublishSnapshot(const FTramMotionState& NewSnapshot);
};
