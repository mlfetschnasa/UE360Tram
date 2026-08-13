#include "TramMotionMath.h"

namespace
{
	// Smallest non-negative root of A*t^2 + B*t + C = 0, or -1.0 if none exists.
	double SmallestNonNegativeRoot(double A, double B, double C)
	{
		if (FMath::IsNearlyZero(A))
		{
			if (FMath::IsNearlyZero(B))
			{
				return -1.0;
			}
			const double T = -C / B;
			return (T >= 0.0) ? T : -1.0;
		}

		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant < 0.0)
		{
			return -1.0;
		}
		const double SqrtDisc = FMath::Sqrt(Discriminant);
		const double T1 = (-B + SqrtDisc) / (2.0 * A);
		const double T2 = (-B - SqrtDisc) / (2.0 * A);
		const double Lo = FMath::Min(T1, T2);
		const double Hi = FMath::Max(T1, T2);
		if (Lo >= 0.0)
		{
			return Lo;
		}
		if (Hi >= 0.0)
		{
			return Hi;
		}
		return -1.0;
	}
}

void TramMotionMath::AdvanceTowardTarget(
	double StartSpeed,
	double TargetSpeed,
	double AccelRate,
	double DecelRate,
	double TimeBudget,
	double DistanceBudget,
	double& OutTimeConsumed,
	double& OutDistanceCovered,
	double& OutEndSpeed,
	bool& bOutHitDistanceCap)
{
	TimeBudget = FMath::Max(0.0, TimeBudget);
	DistanceBudget = FMath::Max(0.0, DistanceBudget);
	StartSpeed = FMath::Max(0.0, StartSpeed);
	TargetSpeed = FMath::Max(0.0, TargetSpeed);

	if (TimeBudget <= 0.0 || DistanceBudget <= 0.0)
	{
		OutTimeConsumed = 0.0;
		OutDistanceCovered = 0.0;
		OutEndSpeed = StartSpeed;
		bOutHitDistanceCap = (DistanceBudget <= 0.0);
		return;
	}

	const bool bAccelerating = TargetSpeed >= StartSpeed;
	const double Rate = FMath::Max(0.0, bAccelerating ? AccelRate : DecelRate);
	const double Sign = bAccelerating ? 1.0 : -1.0;

	// No ramp configured for this direction: speed jumps to TargetSpeed immediately, then
	// travels at that constant speed for the remainder of the budget.
	if (Rate <= UE_DOUBLE_SMALL_NUMBER)
	{
		if (TargetSpeed <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutTimeConsumed = TimeBudget;
			OutDistanceCovered = 0.0;
			OutEndSpeed = 0.0;
			bOutHitDistanceCap = false;
			return;
		}

		const double DistanceIfFullTime = TargetSpeed * TimeBudget;
		if (DistanceIfFullTime <= DistanceBudget)
		{
			OutTimeConsumed = TimeBudget;
			OutDistanceCovered = DistanceIfFullTime;
			OutEndSpeed = TargetSpeed;
			bOutHitDistanceCap = false;
		}
		else
		{
			OutTimeConsumed = DistanceBudget / TargetSpeed;
			OutDistanceCovered = DistanceBudget;
			OutEndSpeed = TargetSpeed;
			bOutHitDistanceCap = true;
		}
		return;
	}

	// Ramp phase: time (bounded by TimeBudget) until TargetSpeed would be reached.
	const double RampTimeToTarget = FMath::Abs(TargetSpeed - StartSpeed) / Rate;
	const double RampTime = FMath::Min(RampTimeToTarget, TimeBudget);
	const double RampDistance = StartSpeed * RampTime + 0.5 * Sign * Rate * RampTime * RampTime;

	if (RampDistance > DistanceBudget)
	{
		// The distance cap is reached during the ramp itself. Solve
		// 0.5*Sign*Rate*t^2 + StartSpeed*t - DistanceBudget = 0 for the smallest t >= 0.
		double T = SmallestNonNegativeRoot(0.5 * Sign * Rate, StartSpeed, -DistanceBudget);
		T = FMath::Clamp(T < 0.0 ? 0.0 : T, 0.0, RampTime);

		OutTimeConsumed = T;
		OutDistanceCovered = DistanceBudget;
		OutEndSpeed = FMath::Max(0.0, StartSpeed + Sign * Rate * T);
		bOutHitDistanceCap = true;
		return;
	}

	if (RampTime < RampTimeToTarget)
	{
		// TimeBudget ran out mid-ramp, before reaching TargetSpeed and before the distance cap.
		OutTimeConsumed = RampTime;
		OutDistanceCovered = RampDistance;
		OutEndSpeed = StartSpeed + Sign * Rate * RampTime;
		bOutHitDistanceCap = false;
		return;
	}

	// Ramp completed (reached TargetSpeed) with time to spare: travel the remainder at
	// constant TargetSpeed.
	const double RemainingTime = TimeBudget - RampTime;
	const double RemainingDistanceBudget = DistanceBudget - RampDistance;

	if (TargetSpeed <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutTimeConsumed = TimeBudget;
		OutDistanceCovered = RampDistance;
		OutEndSpeed = 0.0;
		bOutHitDistanceCap = false;
		return;
	}

	const double ConstantPhaseDistanceIfFullTime = TargetSpeed * RemainingTime;
	if (ConstantPhaseDistanceIfFullTime <= RemainingDistanceBudget)
	{
		OutTimeConsumed = TimeBudget;
		OutDistanceCovered = RampDistance + ConstantPhaseDistanceIfFullTime;
		OutEndSpeed = TargetSpeed;
		bOutHitDistanceCap = false;
	}
	else
	{
		const double ConstantPhaseTime = RemainingDistanceBudget / TargetSpeed;
		OutTimeConsumed = RampTime + ConstantPhaseTime;
		OutDistanceCovered = DistanceBudget;
		OutEndSpeed = TargetSpeed;
		bOutHitDistanceCap = true;
	}
}
