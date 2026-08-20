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
		// above. GetAllActorsOfClass (not the single-result GetActorOfClass) so a level that
		// accidentally has more than one ATramViewRig placed fails loudly instead of silently
		// picking "whichever one happened to be first" - same reasoning as
		// UTramDisplayClusterViewSync's RootActor resolution.
		TArray<AActor*> ViewRigCandidates;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATramViewRig::StaticClass(), ViewRigCandidates);
		if (ViewRigCandidates.Num() > 1)
		{
			UE_LOG(LogTramSystem, Warning, TEXT("ATramPlayerController on %s found %d ATramViewRig actors in the level - viewing the first one found, but this level should only have one"), *GetNameSafe(this), ViewRigCandidates.Num());
		}
		ATramViewRig* Rig = ViewRigCandidates.Num() > 0 ? Cast<ATramViewRig>(ViewRigCandidates[0]) : nullptr;
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
				DevPreviewCamera->ViewRig = Rig;
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

	// Falls back to nDisplay's own -dc_node= launch arg (see SETUP.md 7c) so a production
	// machine only needs ONE consistent per-machine switch instead of two that have to be kept
	// in sync by hand - this is purely reading a well-known command-line token as plain text
	// (FParse doesn't touch anything DisplayCluster-specific), so it does not create a
	// dependency on that plugin/module (Objective 19 stays intact). -dc_node= is a string key
	// that must match a node ID in the nDisplay config exactly (nDisplay looks it up by ID, not
	// position), and nDisplay's own Configurator tool defaults to naming nodes "Node_0".."Node_3"
	// rather than plain integers - so this extracts the trailing run of digits (e.g. "Node_2" ->
	// 2) instead of requiring the whole value to be numeric. Falls through to the ini/auto-assign
	// options below if -dc_node= is absent or has no trailing digits at all.
	FString DcNodeId;
	if (FParse::Value(FCommandLine::Get(), TEXT("dc_node="), DcNodeId) && !DcNodeId.IsEmpty())
	{
		int32 DigitsStart = DcNodeId.Len();
		while (DigitsStart > 0 && FChar::IsDigit(DcNodeId[DigitsStart - 1]))
		{
			--DigitsStart;
		}
		if (DigitsStart < DcNodeId.Len())
		{
			return FCString::Atoi(*DcNodeId.Mid(DigitsStart));
		}
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
