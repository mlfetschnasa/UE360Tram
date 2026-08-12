// Deterministic, closed-form rate-based kinematics used to evaluate tram motion from an
// authoritative anchor + elapsed time, without per-frame integration. Deliberately free of
// any UE actor/component/networking types so it stays trivially unit-testable and reusable
// (e.g. from a future automation test) - see "Keep Systems Separated" in the objectives.
#pragma once

#include "CoreMinimal.h"

namespace TramMotionMath
{
	// Advances speed from StartSpeed toward TargetSpeed at a constant AccelRate (if
	// accelerating) or DecelRate (if decelerating), for up to TimeBudget seconds, but stops
	// early if DistanceBudget (>= 0) would otherwise be exceeded.
	//
	// All speeds/rates are assumed non-negative (the tram never reverses). Rates of 0 mean
	// "no ramp configured" - speed jumps to TargetSpeed immediately.
	//
	// Outputs:
	//   OutTimeConsumed     <= TimeBudget seconds actually elapsed.
	//   OutDistanceCovered  <= DistanceBudget cm actually travelled.
	//   OutEndSpeed         speed (cm/s) at OutTimeConsumed.
	//   bOutHitDistanceCap  true if DistanceBudget was reached before TimeBudget elapsed.
	TRAMSYSTEM_API void AdvanceTowardTarget(
		double StartSpeed,
		double TargetSpeed,
		double AccelRate,
		double DecelRate,
		double TimeBudget,
		double DistanceBudget,
		double& OutTimeConsumed,
		double& OutDistanceCovered,
		double& OutEndSpeed,
		bool& bOutHitDistanceCap);
}
