#include "TramPlayerController.h"
#include "TramGameMode.h"
#include "TramPlayerState.h"
#include "TramSystem.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

void ATramPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		RequestTramSlot(ResolveInitialDesiredSlot());
	}
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
