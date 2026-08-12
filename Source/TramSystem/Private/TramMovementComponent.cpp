#include "TramMovementComponent.h"
#include "TramSplineRoute.h"
#include "TramMotionMath.h"
#include "TramSyncTime.h"
#include "TramSystem.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Net/UnrealNetwork.h"

namespace
{
	FString ToDiagnosticString(ETramMovementState State)
	{
		switch (State)
		{
		case ETramMovementState::WaitingForLaunch: return TEXT("WaitingForLaunch");
		case ETramMovementState::Running: return TEXT("Running");
		case ETramMovementState::Paused: return TEXT("Paused");
		case ETramMovementState::Stopped: return TEXT("Stopped");
		default: return TEXT("Unknown");
		}
	}

	FString ToDiagnosticString(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Authority");
		case ROLE_AutonomousProxy: return TEXT("AutonomousProxy");
		case ROLE_SimulatedProxy: return TEXT("SimulatedProxy");
		default: return TEXT("None");
		}
	}
}

UTramMovementComponent::UTramMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	SetIsReplicatedByDefault(true);
}

void UTramMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTramMovementComponent, CurrentSnapshot);
}

double UTramMovementComponent::GetSynchronizedServerTimeSeconds() const
{
	return TramSyncTime::GetSynchronizedServerTimeSeconds(GetWorld());
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

	if (AActor* Owner = GetOwner())
	{
		// The tram is the one shared object every rider must always see; don't let UE's
		// distance-based relevancy culling ever drop it for a far-away/edge-case viewpoint.
		Owner->bAlwaysRelevant = true;

		if (HasControlAuthority() && !Owner->GetIsReplicated())
		{
			UE_LOG(LogTramSystem, Warning, TEXT("Tram actor %s does not have bReplicates=true; tram state will not synchronize to any connected clients"), *GetNameSafe(Owner));
		}
	}

	// Only the authority establishes the starting snapshot. Non-authority machines must not
	// touch CurrentSnapshot here: for a late-joining client, BeginPlay is not guaranteed to
	// run after the actor's initial replicated properties arrive, and stomping a
	// possibly-already-correct (e.g. Running) value back to WaitingForLaunch would break
	// Objective 9 (late join must synchronize without disturbing anything). Non-authority
	// machines simply wait for OnRep_CurrentSnapshot / the initial replicated value.
	if (HasControlAuthority())
	{
		CurrentSnapshot = FTramMotionState();
		CurrentSnapshot.RouteId = Route ? Route->RouteId : NAME_None;
		CurrentSnapshot.ServerTimestamp = GetSynchronizedServerTimeSeconds();
		CurrentSnapshot.MovementState = ETramMovementState::WaitingForLaunch;

		if (Route && Route->IsRouteValid())
		{
			CurrentSnapshot.DistanceAlongSplineCm = Route->NormalizeDistanceCm(StartingDistanceCms);
			CurrentSnapshot.CurrentSegmentIndex = Route->GetSegmentIndexAtDistance(static_cast<float>(CurrentSnapshot.DistanceAlongSplineCm));
			CurrentSnapshot.TargetSpeedCms = Route->GetSegmentTargetSpeedCms(CurrentSnapshot.CurrentSegmentIndex);
		}
	}

	if (Route && Route->IsRouteValid())
	{
		EvaluateAndApply(GetSynchronizedServerTimeSeconds());
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
			// accumulation and is also the network replication trigger (property replication
			// only sends when the value actually changes).
			double Distance, Speed;
			int32 Segment;
			EvaluateClamped(CurrentSnapshot, Now, Distance, Speed, Segment);

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

FTransform UTramMovementComponent::GetAuthoritativeTransformAtServerTime(double QueryServerTime) const
{
	if (!Route)
	{
		return FTransform::Identity;
	}

	double Distance, Speed;
	int32 Segment;
	EvaluateClamped(CurrentSnapshot, QueryServerTime, Distance, Speed, Segment);

	return Route->GetTransformAtDistanceCm(static_cast<float>(Distance));
}

void UTramMovementComponent::EvaluateAndApply(double QueryServerTime)
{
	double Distance, Speed;
	int32 Segment;
	EvaluateClamped(CurrentSnapshot, QueryServerTime, Distance, Speed, Segment);

	if (bCorrectionBlendActive)
	{
		const double Alpha = (CorrectionBlendDurationSeconds > UE_DOUBLE_SMALL_NUMBER)
			? FMath::Clamp((QueryServerTime - CorrectionBlendStartServerTime) / CorrectionBlendDurationSeconds, 0.0, 1.0)
			: 1.0;

		if (Alpha >= 1.0)
		{
			bCorrectionBlendActive = false;
		}
		else
		{
			// Blend from where the *old* anchor's trajectory would be right now toward where
			// the new authoritative anchor's trajectory is right now. Both sides are
			// deterministic functions of this machine's own snapshots, so the blend is
			// smooth without needing to match any other machine's transient blend state -
			// only the post-blend result needs to converge, which it does by construction.
			double PreDistance, PreSpeed;
			int32 PreSegment;
			EvaluateClamped(PreviousSnapshotForBlend, QueryServerTime, PreDistance, PreSpeed, PreSegment);

			Distance = FMath::Lerp(PreDistance, Distance, Alpha);
			Speed = FMath::Lerp(PreSpeed, Speed, Alpha);
		}
	}

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

void UTramMovementComponent::EvaluateClamped(const FTramMotionState& Snapshot, double QueryServerTime, double& OutDistanceCm, double& OutSpeedCms, int32& OutSegmentIndex) const
{
	double Elapsed = QueryServerTime - Snapshot.ServerTimestamp;

	if (Elapsed > MaxExtrapolationSeconds)
	{
		if (QueryServerTime - LastStaleSnapshotWarningTime > 1.0)
		{
			UE_LOG(LogTramSystem, Warning, TEXT("Tram snapshot on %s is %.2fs stale (cap %.2fs) - clamping extrapolation. Check network/replication health."),
				*GetNameSafe(GetOwner()), Elapsed, MaxExtrapolationSeconds);
			LastStaleSnapshotWarningTime = QueryServerTime;
		}
		Elapsed = MaxExtrapolationSeconds;
	}
	else if (Elapsed < 0.0)
	{
		// Synchronized time moving backward relative to this anchor (clock correction, or a
		// snapshot arriving stamped slightly in what looks like "the future" due to jitter):
		// hold at the anchor rather than evaluating a negative-time motion.
		Elapsed = 0.0;
	}

	Evaluate(Snapshot, Elapsed, OutDistanceCm, OutSpeedCms, OutSegmentIndex);
}

void UTramMovementComponent::PublishSnapshot(const FTramMotionState& NewSnapshot)
{
	CurrentSnapshot = NewSnapshot;
	TimeSinceLastPublish = 0.0;

	UE_LOG(LogTramSystem, Verbose, TEXT("Tram snapshot published: state=%d dist=%.1f speed=%.1f target=%.1f segment=%d t=%.3f"),
		(int32)NewSnapshot.MovementState, NewSnapshot.DistanceAlongSplineCm, NewSnapshot.CurrentSpeedCms,
		NewSnapshot.TargetSpeedCms, NewSnapshot.CurrentSegmentIndex, NewSnapshot.ServerTimestamp);
}

void UTramMovementComponent::OnRep_CurrentSnapshot(FTramMotionState OldSnapshot)
{
	if (Route && CurrentSnapshot.RouteId != NAME_None && Route->RouteId != CurrentSnapshot.RouteId)
	{
		UE_LOG(LogTramSystem, Error, TEXT("UTramMovementComponent on %s received a snapshot for route '%s' but is configured with route '%s' - positions will be wrong until this is fixed"),
			*GetNameSafe(GetOwner()), *CurrentSnapshot.RouteId.ToString(), *Route->RouteId.ToString());
	}

	if (!bHasReceivedAnySnapshot)
	{
		// First snapshot this machine has ever seen (typically a late join): adopt directly.
		// There is no prior local prediction to compare against, so measuring "error" here
		// would be meaningless - see Objective 9 (late joiners synchronize instantly).
		bHasReceivedAnySnapshot = true;
		bCorrectionBlendActive = false;
		LastPredictionErrorCm = 0.0;
		LastCorrectionAmountCm = 0.0;
		UE_LOG(LogTramSystem, Log, TEXT("Tram on %s received initial synchronized state: state=%s dist=%.1f"),
			*GetNameSafe(GetOwner()), *ToDiagnosticString(CurrentSnapshot.MovementState), CurrentSnapshot.DistanceAlongSplineCm);
		return;
	}

	// Prediction error = what the new authoritative snapshot says vs. what this machine would
	// have predicted (from its old anchor) for that same synchronized timestamp.
	double PredictedDistance, PredictedSpeed;
	int32 PredictedSegment;
	EvaluateClamped(OldSnapshot, CurrentSnapshot.ServerTimestamp, PredictedDistance, PredictedSpeed, PredictedSegment);

	const double Error = CurrentSnapshot.DistanceAlongSplineCm - PredictedDistance;
	LastPredictionErrorCm = Error;

	const double AbsError = FMath::Abs(Error);
	const double Now = GetSynchronizedServerTimeSeconds();

	if (AbsError <= CorrectionSnapThresholdCm)
	{
		bCorrectionBlendActive = false;
		LastCorrectionAmountCm = 0.0;
	}
	else if (AbsError <= CorrectionSevereThresholdCm)
	{
		PreviousSnapshotForBlend = OldSnapshot;
		CorrectionBlendStartServerTime = Now;
		bCorrectionBlendActive = true;
		LastCorrectionAmountCm = Error;
		UE_LOG(LogTramSystem, Verbose, TEXT("Tram on %s correcting %.1fcm over %.2fs"), *GetNameSafe(GetOwner()), Error, CorrectionBlendDurationSeconds);
	}
	else
	{
		bCorrectionBlendActive = false;
		LastCorrectionAmountCm = Error;
		UE_LOG(LogTramSystem, Warning, TEXT("Tram on %s: severe prediction error %.1fcm (threshold %.1fcm) - snapping instantly"),
			*GetNameSafe(GetOwner()), Error, CorrectionSevereThresholdCm);
	}
}

FString UTramMovementComponent::GetDiagnosticSummary() const
{
	const ENetRole LocalRole = GetOwner() ? GetOwner()->GetLocalRole() : ROLE_None;
	return FString::Printf(TEXT("Role=%s State=%s Dist=%.1fcm Speed=%.1f/%.1fcm/s Seg=%d PredErr=%.1fcm Blend=%s"),
		*ToDiagnosticString(LocalRole), *ToDiagnosticString(CurrentSnapshot.MovementState),
		LastEvaluatedDistanceCm, LastEvaluatedSpeedCms, CurrentSnapshot.TargetSpeedCms, LastEvaluatedSegmentIndex,
		LastPredictionErrorCm, bCorrectionBlendActive ? TEXT("true") : TEXT("false"));
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
	EvaluateClamped(CurrentSnapshot, Now, Distance, Speed, Segment);

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
	EvaluateClamped(CurrentSnapshot, Now, Distance, Speed, Segment);

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
