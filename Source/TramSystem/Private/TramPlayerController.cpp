#include "TramPlayerController.h"
#include "TramGameMode.h"
#include "TramPlayerState.h"
#include "TramViewRig.h"
#include "TramSystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

void ATramPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		RequestTramSlot(ResolveInitialDesiredSlot());

		// No per-player Pawn is spawned (see ATramGameMode) - every rider's camera targets
		// the one shared ATramViewRig directly instead (Objective 27's simplified single-
		// camera dev mode, via ATramViewRig::CalcCamera). This only affects this machine's
		// own local view, so it's resolved locally rather than over the network, same as
		// ResolveInitialDesiredSlot() above.
		if (AActor* Rig = UGameplayStatics::GetActorOfClass(GetWorld(), ATramViewRig::StaticClass()))
		{
			SetViewTargetWithBlend(Rig, 0.f);
		}
		else
		{
			UE_LOG(LogTramSystem, Warning, TEXT("ATramPlayerController on %s found no ATramViewRig in the level to view"), *GetNameSafe(this));
		}
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
