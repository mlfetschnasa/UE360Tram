#include "TramViewRig.h"
#include "TramMovementComponent.h"
#include "TramSyncTime.h"
#include "TramSystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
	FString ToDiagnosticString(ETramLookAxisMode Mode)
	{
		switch (Mode)
		{
		case ETramLookAxisMode::Disabled: return TEXT("Disabled");
		case ETramLookAxisMode::YawOnly: return TEXT("YawOnly");
		case ETramLookAxisMode::PitchOnly: return TEXT("PitchOnly");
		case ETramLookAxisMode::YawPitch: return TEXT("YawPitch");
		case ETramLookAxisMode::YawPitchRoll: return TEXT("YawPitchRoll");
		default: return TEXT("Unknown");
		}
	}
}

ATramViewRig::ATramViewRig()
{
	PrimaryActorTick.bCanEverTick = true;

	// Unlike UTramMovementComponent (reusable on an arbitrary host Actor), this is our own
	// Actor class, so we can and should set this directly rather than just warning if it's
	// missing.
	bReplicates = true;

	// The shared view rig's state must always be visible to every rider, regardless of UE's
	// distance-based relevancy culling.
	bAlwaysRelevant = true;
}

void ATramViewRig::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATramViewRig, LookRotationState);
	DOREPLIFETIME(ATramViewRig, LookAxisMode);
}

bool ATramViewRig::HasControlAuthority() const
{
	return HasAuthority();
}

double ATramViewRig::GetSynchronizedServerTimeSeconds() const
{
	return TramSyncTime::GetSynchronizedServerTimeSeconds(GetWorld());
}

UTramMovementComponent* ATramViewRig::ResolveTramMovementComponent() const
{
	return CachedTramMovementComponent;
}

void ATramViewRig::BeginPlay()
{
	Super::BeginPlay();

	CachedTramMovementComponent = TramActor ? TramActor->FindComponentByClass<UTramMovementComponent>() : nullptr;
	if (!CachedTramMovementComponent)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("ATramViewRig %s has no valid TramActor with a UTramMovementComponent"), *GetNameSafe(this));
	}

	if (HasControlAuthority())
	{
		const double Now = GetSynchronizedServerTimeSeconds();
		LookRotationState = FTramLookRotationState();
		LookRotationState.StartRotation = FQuat::Identity;
		LookRotationState.TargetRotation = FQuat::Identity;
		LookRotationState.TransitionStartServerTime = Now;
		LookRotationState.TransitionDurationSeconds = 0.0;
	}
}

void ATramViewRig::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasControlAuthority())
	{
		return;
	}

	TimeSinceLastLookPublish += DeltaTime;
	if (TimeSinceLastLookPublish >= LookPublishIntervalSeconds)
	{
		TimeSinceLastLookPublish = 0.0;

		const double Now = GetSynchronizedServerTimeSeconds();
		const FQuat NewTarget = DesiredLookRotator.Quaternion();

		if (!NewTarget.Equals(LookRotationState.TargetRotation, UE_KINDA_SMALL_NUMBER))
		{
			PublishLookTransition(NewTarget, Now);
		}
	}
}

FQuat ATramViewRig::EvaluateLookRotation(const FTramLookRotationState& State, double Now) const
{
	if (State.TransitionDurationSeconds <= 0.0)
	{
		return State.TargetRotation;
	}

	const double Alpha = FMath::Clamp((Now - State.TransitionStartServerTime) / State.TransitionDurationSeconds, 0.0, 1.0);
	return FQuat::Slerp(State.StartRotation, State.TargetRotation, Alpha).GetNormalized();
}

void ATramViewRig::PublishLookTransition(const FQuat& NewTarget, double Now)
{
	FTramLookRotationState NewState;
	// Continue from wherever evaluation of the *previous* transition currently sits, so
	// consecutive publishes chain smoothly with no pop even if publish cadence jitters.
	NewState.StartRotation = EvaluateLookRotation(LookRotationState, Now);
	NewState.TargetRotation = NewTarget;
	NewState.TransitionStartServerTime = Now;
	NewState.TransitionDurationSeconds = LookPublishIntervalSeconds;

	LookRotationState = NewState;
}

void ATramViewRig::ApplyOperatorLookInput(float YawDelta, float PitchDelta, float RollDelta)
{
	if (!HasControlAuthority() || LookAxisMode == ETramLookAxisMode::Disabled)
	{
		return;
	}

	if (LookAxisMode == ETramLookAxisMode::YawOnly || LookAxisMode == ETramLookAxisMode::YawPitch || LookAxisMode == ETramLookAxisMode::YawPitchRoll)
	{
		DesiredLookRotator.Yaw = FMath::Fmod(DesiredLookRotator.Yaw + YawDelta * MouseYawSensitivity, 360.0);
	}

	if (LookAxisMode == ETramLookAxisMode::PitchOnly || LookAxisMode == ETramLookAxisMode::YawPitch || LookAxisMode == ETramLookAxisMode::YawPitchRoll)
	{
		DesiredLookRotator.Pitch = FMath::Clamp(DesiredLookRotator.Pitch + PitchDelta * MousePitchSensitivity, -MaxPitchDegrees, MaxPitchDegrees);
	}

	if (LookAxisMode == ETramLookAxisMode::YawPitchRoll)
	{
		DesiredLookRotator.Roll = FMath::Fmod(DesiredLookRotator.Roll + RollDelta, 360.0);
	}
}

void ATramViewRig::SetLookAxisMode(ETramLookAxisMode NewMode)
{
	if (!HasControlAuthority())
	{
		return;
	}
	LookAxisMode = NewMode;
	UE_LOG(LogTramSystem, Log, TEXT("Tram look axis mode set to %s"), *ToDiagnosticString(LookAxisMode));
}

FQuat ATramViewRig::GetSharedLookRotation() const
{
	return EvaluateLookRotation(LookRotationState, GetSynchronizedServerTimeSeconds());
}

FQuat ATramViewRig::GetSmoothedObserverBaseRotation() const
{
	const UTramMovementComponent* MoveComp = ResolveTramMovementComponent();
	if (!MoveComp)
	{
		return FQuat::Identity;
	}

	const double Now = GetSynchronizedServerTimeSeconds();
	const double LagSeconds = bEnableRotationFollowSmoothing ? FMath::Max(0.0, static_cast<double>(RotationFollowLagSeconds)) : 0.0;

	return MoveComp->GetAuthoritativeTransformAtServerTime(Now - LagSeconds).GetRotation();
}

FTransform ATramViewRig::GetSharedObserverTransform() const
{
	const UTramMovementComponent* MoveComp = ResolveTramMovementComponent();
	const AActor* TramOwner = MoveComp ? MoveComp->GetOwner() : nullptr;
	if (!MoveComp || !TramOwner)
	{
		return FTransform::Identity;
	}

	// The observer rides rigidly with the tram body - its position is unaffected by rotation
	// smoothing, only its facing direction is (see class header comment for the full
	// composition order).
	const FTransform TramTransform = TramOwner->GetActorTransform();
	const FVector ObserverLocation = TramTransform.TransformPosition(ObserverOffset.GetLocation());

	const FQuat SmoothedBase = GetSmoothedObserverBaseRotation();
	const FQuat SharedLook = GetSharedLookRotation();
	const FQuat FinalRotation = (SmoothedBase * ObserverOffset.GetRotation() * SharedLook).GetNormalized();

	return FTransform(FinalRotation, ObserverLocation);
}

FString ATramViewRig::GetDiagnosticSummary() const
{
	const FRotator Look = GetSharedLookRotation().Rotator();
	const FRotator SmoothedBase = GetSmoothedObserverBaseRotation().Rotator();

	return FString::Printf(TEXT("LookMode=%s SharedLook(Y=%.1f P=%.1f R=%.1f) SmoothedBaseYaw=%.1f"),
		*ToDiagnosticString(LookAxisMode), Look.Yaw, Look.Pitch, Look.Roll, SmoothedBase.Yaw);
}
