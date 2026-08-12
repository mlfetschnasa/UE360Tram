// Core enums/structs shared by tram simulation and (later) network synchronization.
//
// FTramMotionState is the compact authoritative "anchor" used everywhere: the server publishes
// one whenever tram state changes (launch/pause/resume/stop, segment transitions, periodic
// re-anchoring) and every machine - including the server itself - derives the current tram
// position by evaluating this anchor against synchronized server time rather than integrating
// per-frame deltas. See UTramMovementComponent for the evaluation rules.
#pragma once

#include "CoreMinimal.h"
#include "TramTypes.generated.h"

UENUM(BlueprintType)
enum class ETramMovementState : uint8
{
	WaitingForLaunch UMETA(DisplayName = "Waiting For Launch"),
	Running UMETA(DisplayName = "Running"),
	Paused UMETA(DisplayName = "Paused"),
	Stopped UMETA(DisplayName = "Stopped"),
};

// Per spline-point metadata. The speed assigned to point N applies to the outgoing segment
// N -> N+1.
USTRUCT(BlueprintType)
struct FTramSplinePointMetadata
{
	GENERATED_BODY()

	// Target speed (cm/s) for the segment starting at this point.
	// A negative value means "use the owning route's DefaultTargetSpeedCms".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram")
	float TargetSpeedCms = -1.f;
};

// Compact authoritative snapshot of tram motion. Conceptually matches Objective 14.
// Every machine reconstructs current tram position from this struct plus synchronized
// server time - see UTramMovementComponent::Evaluate. Only the server ever writes this
// struct's authoritative copy; everyone else treats it as read-only input.
USTRUCT(BlueprintType)
struct FTramMotionState
{
	GENERATED_BODY()

	// Identifies which route this snapshot is relative to. Used defensively to detect a
	// misconfigured/mismatched route on the evaluating machine (see Objective: "Route
	// reference invalid" failure case).
	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	FName RouteId = NAME_None;

	// Synchronized server-world time (seconds, see AGameStateBase::GetServerWorldTimeSeconds)
	// at which the rest of this snapshot's values were true.
	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	double ServerTimestamp = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	double DistanceAlongSplineCm = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	double CurrentSpeedCms = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	double TargetSpeedCms = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	int32 CurrentSegmentIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tram")
	ETramMovementState MovementState = ETramMovementState::WaitingForLaunch;
};
