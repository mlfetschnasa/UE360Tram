#include "TramSyncHUD.h"
#include "TramSystem.h"
#include "TramTypes.h"
#include "TramViewRig.h"
#include "TramMovementComponent.h"
#include "TramDisplayConfiguration.h"
#include "TramGameState.h"
#include "TramPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

void ATramSyncHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!ViewRig)
	{
		// Same "auto-assign only when unambiguous" search UTramDisplayClusterViewSync uses for
		// its RootActor - see that class for the reasoning.
		TArray<AActor*> Candidates;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATramViewRig::StaticClass(), Candidates);

		if (Candidates.Num() == 1)
		{
			ViewRig = Cast<ATramViewRig>(Candidates[0]);
		}
		else if (Candidates.Num() > 1)
		{
			UE_LOG(LogTramSystem, Warning, TEXT("ATramSyncHUD found %d ATramViewRig candidates in the level - set ViewRig explicitly."), Candidates.Num());
		}
	}
}

UTramMovementComponent* ATramSyncHUD::ResolveTramMovementComponent() const
{
	if (!ViewRig || !ViewRig->TramActor)
	{
		return nullptr;
	}
	return ViewRig->TramActor->FindComponentByClass<UTramMovementComponent>();
}

void ATramSyncHUD::DrawLine(const FString& Text, const FColor& Color)
{
	if (!Canvas)
	{
		return;
	}
	Canvas->SetDrawColor(Color);
	Canvas->DrawText(GEngine->GetSmallFont(), Text, 20.f, NextLineY);
	NextLineY += 18.f;
}

void ATramSyncHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bShowSyncHUD || !Canvas)
	{
		return;
	}

	NextLineY = 40.f;
	DrawLine(TEXT("=== Tram Sync ==="), FColor::Cyan);

	UTramMovementComponent* MovementComponent = ResolveTramMovementComponent();
	if (MovementComponent)
	{
		// Reuse the component's own summary rather than re-deriving each field - it already
		// covers Role/State/Dist/Speed/TargetSpeed/Seg/PredErr/Blend.
		DrawLine(MovementComponent->GetDiagnosticSummary());
		DrawLine(FString::Printf(TEXT("SyncedServerTime=%.2fs LastCorrection=%.1fcm"),
			MovementComponent->GetSynchronizedServerTimeSeconds(), MovementComponent->GetLastCorrectionAmountCm()));
	}
	else
	{
		DrawLine(TEXT("No tram movement component resolved (ViewRig/ViewRig.TramActor not set?)"), FColor::Yellow);
	}

	if (ViewRig)
	{
		// Covers LookMode (active mouse-axis mode)/SharedLook yaw-pitch-roll/SmoothedBaseYaw.
		DrawLine(ViewRig->GetDiagnosticSummary());
	}

	const APlayerController* PC = GetOwningPlayerController();
	const ATramPlayerState* TramPlayerState = PC ? PC->GetPlayerState<ATramPlayerState>() : nullptr;
	const int32 SlotIndex = TramPlayerState ? TramPlayerState->GetSlotIndex() : INDEX_NONE;

	FString DisplaysText = TEXT("-");
	if (DisplayConfiguration && SlotIndex != INDEX_NONE)
	{
		TArray<FString> DisplayStrings;
		for (int32 DisplayIndex : DisplayConfiguration->GetDisplaysForSlot(SlotIndex))
		{
			DisplayStrings.Add(FString::FromInt(DisplayIndex));
		}
		DisplaysText = FString::Join(DisplayStrings, TEXT(","));
	}

	const int32 PingMs = TramPlayerState ? FMath::RoundToInt(TramPlayerState->GetPingInMilliseconds()) : 0;
	DrawLine(FString::Printf(TEXT("Slot=%d Displays=[%s] Ping=%dms"), SlotIndex, *DisplaysText, PingMs));

	if (HasAuthority())
	{
		const ATramGameState* TramGameState = GetWorld() ? GetWorld()->GetGameState<ATramGameState>() : nullptr;
		if (TramGameState)
		{
			TArray<FString> OccupiedStrings;
			for (int32 Slot : TramGameState->GetOccupiedSlots())
			{
				OccupiedStrings.Add(FString::FromInt(Slot));
			}

			const FString LaunchState = MovementComponent
				? StaticEnum<ETramMovementState>()->GetDisplayNameTextByValue(static_cast<int64>(MovementComponent->GetMovementState())).ToString()
				: TEXT("?");
			const int32 RiderCount = TramGameState->PlayerArray.Num();

			DrawLine(TEXT("=== Server ==="), FColor::Cyan);
			DrawLine(FString::Printf(TEXT("Riders=%d OccupiedSlots=[%s] FreeSlot=%d LaunchState=%s"),
				RiderCount, *FString::Join(OccupiedStrings, TEXT(",")), TramGameState->FindFirstFreeSlot(), *LaunchState));
		}
	}
}
