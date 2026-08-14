#include "TramDisplayClusterViewSync.h"
#include "TramViewRig.h"
#include "TramSystemDisplayCluster.h"
#include "DisplayClusterRootActor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/EngineTypes.h"

UTramDisplayClusterViewSync::UTramDisplayClusterViewSync()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTramDisplayClusterViewSync::BeginPlay()
{
	Super::BeginPlay();

	if (!RootActor)
	{
		RootActor = Cast<ADisplayClusterRootActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass()));
	}

	if (!RootActor)
	{
		UE_LOG(LogTramSystemDisplayCluster, Warning, TEXT("UTramDisplayClusterViewSync on %s found no ADisplayClusterRootActor in the level"), *GetNameSafe(GetOwner()));
	}
	if (!ViewRig)
	{
		UE_LOG(LogTramSystemDisplayCluster, Warning, TEXT("UTramDisplayClusterViewSync on %s has no ViewRig assigned"), *GetNameSafe(GetOwner()));
	}

	if (RootActor && ViewRig)
	{
		// Success is otherwise silent (nothing logs on the happy path each tick, to avoid log
		// spam) - this one line makes "it's actually running" provable in the Output Log
		// rather than only inferable from the absence of the warnings above.
		UE_LOG(LogTramSystemDisplayCluster, Log, TEXT("UTramDisplayClusterViewSync on %s is syncing %s to %s's shared observer transform"),
			*GetNameSafe(GetOwner()), *GetNameSafe(RootActor), *GetNameSafe(ViewRig));
	}
}

void UTramDisplayClusterViewSync::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!RootActor || !ViewRig)
	{
		return;
	}

	const FTransform ObserverTransform = ViewRig->GetSharedObserverTransform();
	// TeleportPhysics: same reasoning as UTramMovementComponent/ATramSlotPreviewCamera - this
	// is scripted movement driven by a deterministic external computation, not physics.
	RootActor->SetActorLocationAndRotation(ObserverTransform.GetLocation(), ObserverTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);

	if (DiagnosticLogIntervalSeconds > 0.f)
	{
		TimeSinceLastDiagnosticLog += DeltaTime;
		if (TimeSinceLastDiagnosticLog >= DiagnosticLogIntervalSeconds)
		{
			TimeSinceLastDiagnosticLog = 0.0;
			const FVector Location = ObserverTransform.GetLocation();
			const FRotator Rotation = ObserverTransform.GetRotation().Rotator();
			UE_LOG(LogTramSystemDisplayCluster, Log, TEXT("%s -> Location=(X=%.1f,Y=%.1f,Z=%.1f) Rotation=(Pitch=%.1f,Yaw=%.1f,Roll=%.1f)"),
				*GetNameSafe(RootActor), Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
		}
	}
}
