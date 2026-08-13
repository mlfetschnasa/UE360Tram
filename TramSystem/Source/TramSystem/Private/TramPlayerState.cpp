#include "TramPlayerState.h"
#include "TramSystem.h"
#include "Net/UnrealNetwork.h"

void ATramPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATramPlayerState, SlotIndex);
}

void ATramPlayerState::SetSlotIndex(int32 NewSlotIndex)
{
	if (!HasAuthority() || SlotIndex == NewSlotIndex)
	{
		return;
	}

	SlotIndex = NewSlotIndex;
	OnTramSlotChanged.Broadcast(SlotIndex);

	UE_LOG(LogTramSystem, Verbose, TEXT("%s slot index set to %d"), *GetNameSafe(this), SlotIndex);
}

void ATramPlayerState::OnRep_SlotIndex()
{
	OnTramSlotChanged.Broadcast(SlotIndex);
}
