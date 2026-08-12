#include "TramMovementComponent.h"
#include "TramSplineRoute.h"
#include "TramMotionMath.h"
#include "TramSystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"

UTramMovementComponent::UTramMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

double UTramMovementComponent::GetSynchronizedServerTimeSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			// AGameStateBase already tracks and compensates for the client/server clock
			// offset over the network connection; it degrades to plain world time when
			// there is no networking (standalone/listen-server-with-no-clients). Reusing
			// it avoids inventing a parallel time-sync mechanism - see Rule 14.
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0;
}

bool UTramMovementComponent::HasControlAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UTramMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!Route)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("UTramMovementComponent on %s has no Route assigned"), *GetNameSafe(GetOwner()));
	}
	else if (!Route->IsRouteValid())
	{
		UE_LOG(LogTramSystem, Warning, TEXT("UTramMovementComponent on %s: Route %s is not valid (needs >= 2 spline points and nonzero length)"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Route));
	}

	CurrentSnapshot = FTramMotionState();
	CurrentSnapshot.RouteId = Route ? Route->RouteId : NAME_None;
	CurrentSnapshot.ServerTimestamp = GetSynchronizedServerTimeSeconds();
	CurrentSnapshot.MovementState = ETramMovementState::WaitingForLaunch;

	if (Route && Route->IsRouteValid())
	{
		CurrentSnapshot.DistanceAlongSplineCm = Route->NormalizeDistanceCm(StartingDistanceCms);
		CurrentSnapshot.CurrentSegmentIndex = Route->GetSegmentIndexAtDistance(static_cast<float>(CurrentSnapshot.DistanceAlongSplineCm));
		CurrentSnapshot.TargetSpeedCms = Route->GetSegmentTargetSpeedCms(CurrentSnapshot.CurrentSegmentIndex);

		EvaluateAndApply(CurrentSnapshot.ServerTimestamp);
	}
}

void UTramMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Route || !Route->IsRouteValid())
	{
		return;
	}

	const double Now = GetSynchronizedServerTimeSeconds();
	EvaluateAndApply(Now);

	if (HasControlAuthority() && CurrentSnapshot.MovementState == ETramMovementState::Running)
	{
		TimeSinceLastPublish += DeltaTime;
		if (TimeSinceLastPublish >= SnapshotPublishIntervalSeconds)
		{
			// Re-anchor to the current evaluated state. This bounds extrapolation error
			// accumulation and, from Phase 2 onward, is also the network publish cadence.
			double Distance, Speed;
			int32 Segment;
			Evaluate(CurrentSnapshot, Now - CurrentSnapshot.ServerTimestamp, Distance, Speed, Segment);

			FTramMotionState Reanchored = CurrentSnapshot;
			Reanchored.ServerTimestamp = Now;
			Reanchored.DistanceAlongSplineCm = Distance;
			Reanchored.CurrentSpeedCms = Speed;
			Reanchored.CurrentSegmentIndex = Segment;
			Reanchored.TargetSpeedCms = Route->GetSegmentTargetSpeedCms(Segment);

			PublishSnapshot(Reanchored);
		}
	}
}

void UTramMovementComponent::EvaluateAndApply(double QueryServerTime)
{
	double Distance, Speed;
	int32 Segment;
	Evaluate(CurrentSnapshot, QueryServerTime - CurrentSnapshot.ServerTimestamp, Distance, Speed, Segment);

	LastEvaluatedDistanceCm = Distance;
	LastEvaluatedSpeedCms = Speed;
	LastEvaluatedSegmentIndex = Segment;

	if (AActor* Owner = GetOwner())
	{
		const FTransform NewTransform = Route->GetTransformAtDistanceCm(static_cast<float>(Distance));
		// TeleportPhysics: this is scripted, spline-driven kinematic movement, not physics
		// simulation - we always want to set the transform directly rather than derive
		// velocity/sweep collision from it.
		Owner->SetActorLocationAndRotation(NewTransform.GetLocation(), NewTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UTramMovementComponent::Evaluate(const FTramMotionState& Snapshot, double ElapsedSeconds, double& OutDistanceCm, double& OutSpeedCms, int32& OutSegmentIndex) const
{
	if (!Route)
	{
		OutDistanceCm = Snapshot.DistanceAlongSplineCm;
		OutSpeedCms = 0.0;
		OutSegmentIndex = Snapshot.CurrentSegmentIndex;
		return;
	}

	if (Snapshot.MovementState != ETramMovementState::Running || ElapsedSeconds <= 0.0)
	{
		// WaitingForLaunch/Paused/Stopped are all frozen: position does not depend on
		// elapsed time. (Documented assumption: Pause freezes instantly with no coast-down;
		// Stop freezes instantly and is terminal until the next LaunchTram.)
		OutDistanceCm = Snapshot.DistanceAlongSplineCm;
		OutSpeedCms = (Snapshot.MovementState == ETramMovementState::Running) ? Snapshot.CurrentSpeedCms : 0.0;
		OutSegmentIndex = Snapshot.CurrentSegmentIndex;
		return;
	}

	double Distance = Snapshot.DistanceAlongSplineCm;
	double Speed = Snapshot.CurrentSpeedCms;
	int32 Segment = Snapshot.CurrentSegmentIndex;
	double Remaining = ElapsedSeconds;

	const int32 NumSegments = FMath::Max(1, Route->GetNumSegments());
	const int32 MaxSteps = NumSegments * 2 + 4;

	int32 Step = 0;
	for (; Step < MaxSteps && Remaining > UE_DOUBLE_SMALL_NUMBER; ++Step)
	{
		const double Target = FMath::Max(0.0, static_cast<double>(Route->GetSegmentTargetSpeedCms(Segment)));
		const double ClampedTarget = (MaxSpeedCms > 0.0) ? FMath::Min(Target, static_cast<double>(MaxSpeedCms)) : Target;
		const double SegmentEnd = static_cast<double>(Route->GetSegmentEndDistanceCm(Segment));
		const double DistanceBudget = FMath::Max(0.0, SegmentEnd - Distance);

		double TimeConsumed, DistanceCovered, EndSpeed;
		bool bHitCap;
		TramMotionMath::AdvanceTowardTarget(Speed, ClampedTarget, AccelerationCmss, DecelerationCmss, Remaining, DistanceBudget, TimeConsumed, DistanceCovered, EndSpeed, bHitCap);

		Distance += DistanceCovered;
		Speed = EndSpeed;
		Remaining -= TimeConsumed;

		if (!bHitCap)
		{
			break;
		}

		const int32 NextSegment = Segment + 1;
		if (NextSegment >= Route->GetNumSegments())
		{
			if (Route->IsClosedLoop())
			{
				Segment = 0;
				Distance = Route->NormalizeDistanceCm(static_cast<float>(Distance));
			}
			else
			{
				// Reached the end of an open route: hold here rather than reversing or
				// wrapping.
				OutDistanceCm = SegmentEnd;
				OutSpeedCms = 0.0;
				OutSegmentIndex = Segment;
				return;
			}
		}
		else
		{
			Segment = NextSegment;
		}
	}

	if (Step >= MaxSteps && Remaining > UE_DOUBLE_SMALL_NUMBER)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("Tram motion evaluation hit its segment-step guard (%d) with %.4fs remaining - check route %s for degenerate (zero-length) segments"),
			MaxSteps, Remaining, *GetNameSafe(Route));
	}

	OutDistanceCm = Distance;
	OutSpeedCms = Speed;
	OutSegmentIndex = Segment;
}

void UTramMovementComponent::PublishSnapshot(const FTramMotionState& NewSnapshot)
{
	CurrentSnapshot = NewSnapshot;
	TimeSinceLastPublish = 0.0;

	UE_LOG(LogTramSystem, Verbose, TEXT("Tram snapshot published: state=%d dist=%.1f speed=%.1f target=%.1f segment=%d t=%.3f"),
		(int32)NewSnapshot.MovementState, NewSnapshot.DistanceAlongSplineCm, NewSnapshot.CurrentSpeedCms,
		NewSnapshot.TargetSpeedCms, NewSnapshot.CurrentSegmentIndex, NewSnapshot.ServerTimestamp);
}

void UTramMovementComponent::LaunchTram()
{
	if (!HasControlAuthority())
	{
		return;
	}
	if (!Route || !Route->IsRouteValid())
	{
		UE_LOG(LogTramSystem, Warning, TEXT("LaunchTram failed: no valid Route assigned to %s"), *GetNameSafe(GetOwner()));
		return;
	}
	if (CurrentSnapshot.MovementState != ETramMovementState::WaitingForLaunch)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("LaunchTram ignored: tram on %s is not WaitingForLaunch (current state=%d)"),
			*GetNameSafe(GetOwner()), (int32)CurrentSnapshot.MovementState);
		return;
	}

	FTramMotionState NewSnapshot;
	NewSnapshot.RouteId = Route->RouteId;
	NewSnapshot.ServerTimestamp = GetSynchronizedServerTimeSeconds();
	NewSnapshot.DistanceAlongSplineCm = Route->NormalizeDistanceCm(StartingDistanceCms);
	NewSnapshot.CurrentSpeedCms = 0.0;
	NewSnapshot.CurrentSegmentIndex = Route->GetSegmentIndexAtDistance(static_cast<float>(NewSnapshot.DistanceAlongSplineCm));
	NewSnapshot.TargetSpeedCms = Route->GetSegmentTargetSpeedCms(NewSnapshot.CurrentSegmentIndex);
	NewSnapshot.MovementState = ETramMovementState::Running;

	PublishSnapshot(NewSnapshot);
	UE_LOG(LogTramSystem, Log, TEXT("Tram launched on %s"), *GetNameSafe(GetOwner()));
}

void UTramMovementComponent::PauseTram()
{
	if (!HasControlAuthority() || !Route || CurrentSnapshot.MovementState != ETramMovementState::Running)
	{
		return;
	}

	const double Now = GetSynchronizedServerTimeSeconds();
	double Distance, Speed;
	int32 Segment;
	Evaluate(CurrentSnapshot, Now - CurrentSnapshot.ServerTimestamp, Distance, Speed, Segment);

	FTramMotionState NewSnapshot = CurrentSnapshot;
	NewSnapshot.ServerTimestamp = Now;
	NewSnapshot.DistanceAlongSplineCm = Distance;
	NewSnapshot.CurrentSpeedCms = Speed; // retained so ResumeTram continues from here
	NewSnapshot.CurrentSegmentIndex = Segment;
	NewSnapshot.MovementState = ETramMovementState::Paused;

	PublishSnapshot(NewSnapshot);
	UE_LOG(LogTramSystem, Log, TEXT("Tram paused on %s at distance=%.1f"), *GetNameSafe(GetOwner()), Distance);
}

void UTramMovementComponent::ResumeTram()
{
	if (!HasControlAuthority() || !Route || CurrentSnapshot.MovementState != ETramMovementState::Paused)
	{
		return;
	}

	FTramMotionState NewSnapshot = CurrentSnapshot;
	NewSnapshot.ServerTimestamp = GetSynchronizedServerTimeSeconds();
	NewSnapshot.TargetSpeedCms = Route->GetSegmentTargetSpeedCms(NewSnapshot.CurrentSegmentIndex);
	NewSnapshot.MovementState = ETramMovementState::Running;

	PublishSnapshot(NewSnapshot);
	UE_LOG(LogTramSystem, Log, TEXT("Tram resumed on %s"), *GetNameSafe(GetOwner()));
}

void UTramMovementComponent::StopTram()
{
	if (!HasControlAuthority())
	{
		return;
	}
	if (CurrentSnapshot.MovementState != ETramMovementState::Running && CurrentSnapshot.MovementState != ETramMovementState::Paused)
	{
		return;
	}

	const double Now = GetSynchronizedServerTimeSeconds();
	double Distance, Speed;
	int32 Segment;
	Evaluate(CurrentSnapshot, Now - CurrentSnapshot.ServerTimestamp, Distance, Speed, Segment);

	FTramMotionState NewSnapshot = CurrentSnapshot;
	NewSnapshot.ServerTimestamp = Now;
	NewSnapshot.DistanceAlongSplineCm = Distance;
	NewSnapshot.CurrentSpeedCms = 0.0;
	NewSnapshot.TargetSpeedCms = 0.0;
	NewSnapshot.CurrentSegmentIndex = Segment;
	NewSnapshot.MovementState = ETramMovementState::Stopped;

	PublishSnapshot(NewSnapshot);
	UE_LOG(LogTramSystem, Log, TEXT("Tram stopped on %s at distance=%.1f"), *GetNameSafe(GetOwner()), Distance);
}
