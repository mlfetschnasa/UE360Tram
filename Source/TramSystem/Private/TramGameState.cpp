#include "TramGameState.h"
#include "TramPlayerState.h"
#include "Net/UnrealNetwork.h"

void ATramGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATramGameState, NumTramSlots);
}

bool ATramGameState::IsSlotOccupied(int32 SlotIndex) const
{
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ATramPlayerState* TramPS = Cast<ATramPlayerState>(PS))
		{
			if (TramPS->GetSlotIndex() == SlotIndex)
			{
				return true;
			}
		}
	}
	return false;
}

int32 ATramGameState::FindFirstFreeSlot() const
{
	for (int32 Slot = 0; Slot < NumTramSlots; ++Slot)
	{
		if (!IsSlotOccupied(Slot))
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

TArray<int32> ATramGameState::GetOccupiedSlots() const
{
	TArray<int32> Result;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ATramPlayerState* TramPS = Cast<ATramPlayerState>(PS))
		{
			if (TramPS->GetSlotIndex() != INDEX_NONE)
			{
				Result.Add(TramPS->GetSlotIndex());
			}
		}
	}
	return Result;
}
