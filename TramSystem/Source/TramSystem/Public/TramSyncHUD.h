// On-screen synchronization diagnostics overlay (Objective 26). A thin AHUD using Canvas::DrawText
// directly rather than UMG, so it needs no content asset to work - consistent with every other
// "wire this into your project yourself" class in this plugin (LaunchTram's button, GameMode/
// PlayerController wiring): set this as your project's AGameModeBase::HUDClass to use it.
//
// Purely a read-only diagnostic: it only reads state already exposed by UTramMovementComponent,
// ATramViewRig, ATramGameState, and ATramPlayerState (several of which already document their
// summary strings as "for HUD/log use") and writes nothing back, so enabling/disabling it can
// never affect synchronization.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TramSyncHUD.generated.h"

class ATramViewRig;
class UTramDisplayConfiguration;
class UTramMovementComponent;

UCLASS()
class TRAMSYSTEM_API ATramSyncHUD : public AHUD
{
	GENERATED_BODY()

public:
	// Off by default so it doesn't clutter production display output unless explicitly enabled -
	// bind ToggleSyncHUD to a debug input action, or set this true directly, from the host project.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Debug")
	bool bShowSyncHUD = false;

	// Auto-resolved in BeginPlay (same single-candidate-only search pattern as
	// UTramDisplayClusterViewSync's RootActor resolution) if left unset.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|Debug")
	TObjectPtr<ATramViewRig> ViewRig;

	// Optional. Only needed to resolve this machine's "Global display indices" line; that line
	// is simply omitted if left unset.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|Debug")
	TObjectPtr<UTramDisplayConfiguration> DisplayConfiguration;

	UFUNCTION(BlueprintCallable, Category = "Tram|Debug")
	void ToggleSyncHUD() { bShowSyncHUD = !bShowSyncHUD; }

	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;

private:
	UTramMovementComponent* ResolveTramMovementComponent() const;
	void DrawLine(const FString& Text, const FColor& Color = FColor::White);

	float NextLineY = 0.f;
};
