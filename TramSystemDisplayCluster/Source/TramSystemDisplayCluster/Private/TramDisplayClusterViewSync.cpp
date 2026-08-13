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
}
