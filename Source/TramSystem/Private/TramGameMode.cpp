#include "TramGameMode.h"
#include "TramPlayerState.h"
#include "TramGameState.h"
#include "TramPlayerController.h"
#include "TramSystem.h"
#include "GameFramework/PlayerState.h"

ATramGameMode::ATramGameMode()
{
	PlayerStateClass = ATramPlayerState::StaticClass();
	GameStateClass = ATramGameState::StaticClass();
	PlayerControllerClass = ATramPlayerController::StaticClass();

	// No per-player Pawn: the installation is one shared tram and one shared observer rig
	// (Objective 1 - "one distributed view rig", not independent per-player viewpoints), so
	// there is nothing for a connecting player to possess. ATramPlayerController::BeginPlay
	// points each player's own camera at the single level-placed ATramViewRig instead.
	// A Blueprint subclass of this GameMode that sets its own DefaultPawnClass in Class
	// Defaults will override this - clear it there too if you run into unexpected pawns.
	DefaultPawnClass = nullptr;
}

bool ATramGameMode::TryAssignTramSlot(APlayerState* ForPlayer, int32 DesiredSlot)
{
	ATramPlayerState* TramPS = Cast<ATramPlayerState>(ForPlayer);
	ATramGameState* TramGS = GetGameState<ATramGameState>();

	if (!TramPS || !TramGS)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("TryAssignTramSlot failed for %s: missing ATramPlayerState or ATramGameState"), *GetNameSafe(ForPlayer));
		return false;
	}

	// Release any slot this rider currently holds before deciding the new one, so a
	// re-request (e.g. changing slot from a UI) can't see itself as "the" occupant of its
	// own previous slot.
	if (TramPS->GetSlotIndex() != INDEX_NONE)
	{
		TramPS->SetSlotIndex(INDEX_NONE);
	}

	int32 SlotToAssign = INDEX_NONE;

	if (DesiredSlot == INDEX_NONE)
	{
		SlotToAssign = TramGS->FindFirstFreeSlot();
	}
	else if (DesiredSlot < 0 || DesiredSlot >= TramGS->NumTramSlots)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("Rejected out-of-range tram slot request %d (valid range 0..%d) from %s - auto-assigning instead"),
			DesiredSlot, TramGS->NumTramSlots - 1, *GetNameSafe(ForPlayer));
		SlotToAssign = TramGS->FindFirstFreeSlot();
	}
	else if (TramGS->IsSlotOccupied(DesiredSlot))
	{
		if (bStrictSlotRequests)
		{
			UE_LOG(LogTramSystem, Warning, TEXT("Tram slot %d already occupied and bStrictSlotRequests is set - leaving %s unassigned"),
				DesiredSlot, *GetNameSafe(ForPlayer));
			SlotToAssign = INDEX_NONE;
		}
		else
		{
			UE_LOG(LogTramSystem, Log, TEXT("Tram slot %d already occupied - auto-assigning %s to another free slot instead"),
				DesiredSlot, *GetNameSafe(ForPlayer));
			SlotToAssign = TramGS->FindFirstFreeSlot();
		}
	}
	else
	{
		SlotToAssign = DesiredSlot;
	}

	TramPS->SetSlotIndex(SlotToAssign);

	if (SlotToAssign == INDEX_NONE)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("No free tram slot available for %s (all %d slots occupied)"), *GetNameSafe(ForPlayer), TramGS->NumTramSlots);
		return false;
	}

	UE_LOG(LogTramSystem, Log, TEXT("Assigned tram slot %d to %s"), SlotToAssign, *GetNameSafe(ForPlayer));
	return true;
}

void ATramGameMode::Logout(AController* Exiting)
{
	if (const APlayerState* PS = Exiting ? Exiting->PlayerState : nullptr)
	{
		if (const ATramPlayerState* TramPS = Cast<ATramPlayerState>(PS))
		{
			// Occupancy is derived live from PlayerArray (see ATramGameState), so the slot is
			// freed automatically once this PlayerState is removed - nothing else to release.
			UE_LOG(LogTramSystem, Log, TEXT("Rider %s disconnecting, freeing tram slot %d"), *GetNameSafe(PS), TramPS->GetSlotIndex());
		}
	}
	Super::Logout(Exiting);
}
