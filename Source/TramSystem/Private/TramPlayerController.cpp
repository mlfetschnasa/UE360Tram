#include "TramPlayerController.h"
#include "TramGameMode.h"
#include "TramPlayerState.h"
#include "TramViewRig.h"
#include "TramDisplayConfiguration.h"
#include "TramSlotPreviewCamera.h"
#include "TramSystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

void ATramPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Covers the case where PlayerState is already valid by the time BeginPlay runs - true for
	// the server's own locally-authoritative controller, since PlayerState is assigned there by
	// a direct function call, not replication, so OnRep_PlayerState below never fires for it.
	BindToPlayerStateSlotChanges();

	if (IsLocalController())
	{
		RequestTramSlot(ResolveInitialDesiredSlot());

		// No per-player Pawn is spawned (see ATramGameMode) - every rider's camera targets the
		// one shared ATramViewRig instead. This only affects this machine's own local view, so
		// it's resolved locally rather than over the network, same as ResolveInitialDesiredSlot()
		// above.
		AActor* Rig = UGameplayStatics::GetActorOfClass(GetWorld(), ATramViewRig::StaticClass());
		if (!Rig)
		{
			UE_LOG(LogTramSystem, Warning, TEXT("ATramPlayerController on %s found no ATramViewRig in the level to view"), *GetNameSafe(this));
			return;
		}

		if (DevPreviewDisplayConfiguration)
		{
			// Objective 27's simplified single-camera-per-machine dev mode: a local, purely
			// this-machine's-own preview camera oriented toward this rider's assigned slot,
			// instead of every machine viewing the identical shared transform directly.
			DevPreviewCamera = GetWorld()->SpawnActor<ATramSlotPreviewCamera>();
			if (DevPreviewCamera)
			{
				DevPreviewCamera->ViewRig = Cast<ATramViewRig>(Rig);
				DevPreviewCamera->DisplayConfiguration = DevPreviewDisplayConfiguration;
				DevPreviewCamera->SlotIndex = GetAssignedTramSlot();
				SetViewTargetWithBlend(DevPreviewCamera, 0.f);
				return;
			}
			UE_LOG(LogTramSystem, Warning, TEXT("ATramPlayerController on %s failed to spawn its ATramSlotPreviewCamera - falling back to the shared view"), *GetNameSafe(this));
		}

		SetViewTargetWithBlend(Rig, 0.f);
	}
}

void ATramPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Covers a remote client, where PlayerState can arrive (or change) after BeginPlay has
	// already run - this is exactly what OnRep_PlayerState exists for.
	BindToPlayerStateSlotChanges();
}

void ATramPlayerController::BindToPlayerStateSlotChanges()
{
	ATramPlayerState* TramPS = Cast<ATramPlayerState>(PlayerState);
	if (TramPS == BoundPlayerState.Get())
	{
		return;
	}

	if (ATramPlayerState* OldPS = BoundPlayerState.Get())
	{
		OldPS->OnTramSlotChanged.RemoveDynamic(this, &ATramPlayerController::HandlePlayerStateSlotChanged);
	}

	BoundPlayerState = TramPS;

	if (TramPS)
	{
		TramPS->OnTramSlotChanged.AddDynamic(this, &ATramPlayerController::HandlePlayerStateSlotChanged);
		// Fire immediately with whatever the slot already is, in case it was assigned before
		// this bind happened - a listener that only binds once at BeginPlay should not have to
		// separately poll GetAssignedTramSlot() to learn the initial value.
		HandlePlayerStateSlotChanged(TramPS->GetSlotIndex());
	}
}

void ATramPlayerController::HandlePlayerStateSlotChanged(int32 NewSlotIndex)
{
	if (DevPreviewCamera)
	{
		DevPreviewCamera->SlotIndex = NewSlotIndex;
	}

	OnLocalTramSlotChanged.Broadcast(NewSlotIndex);
}

int32 ATramPlayerController::ResolveInitialDesiredSlot() const
{
	int32 Value = INDEX_NONE;

	if (FParse::Value(FCommandLine::Get(), TEXT("TramSlot="), Value))
	{
		return Value;
	}

	if (GConfig && GConfig->GetInt(TEXT("TramSystem"), TEXT("PreferredSlot"), Value, GGameIni))
	{
		return Value;
	}

	return INDEX_NONE;
}

void ATramPlayerController::RequestTramSlot(int32 DesiredSlot)
{
	ServerRequestTramSlot(DesiredSlot);
}

void ATramPlayerController::ServerRequestTramSlot_Implementation(int32 DesiredSlot)
{
	if (ATramGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ATramGameMode>() : nullptr)
	{
		GameMode->TryAssignTramSlot(PlayerState, DesiredSlot);
	}
	else
	{
		UE_LOG(LogTramSystem, Warning, TEXT("ServerRequestTramSlot on %s: no ATramGameMode found"), *GetNameSafe(this));
	}
}

int32 ATramPlayerController::GetAssignedTramSlot() const
{
	const ATramPlayerState* TramPS = PlayerState ? Cast<ATramPlayerState>(PlayerState) : nullptr;
	return TramPS ? TramPS->GetSlotIndex() : INDEX_NONE;
}
