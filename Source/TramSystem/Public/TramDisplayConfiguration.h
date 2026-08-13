// Data-driven physical display/installation configuration (Objectives 20-21). A UDataAsset so
// an installation's geometry lives in a designer-editable, host-project-owned asset rather
// than hardcoded constants - the same asset can back the debug visualizer now and the
// production multi-display backend later (Phase 6), without either owning tram simulation or
// being owned by it (see "Keep Systems Separated").
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TramDisplayTypes.h"
#include "TramDisplayConfiguration.generated.h"

UCLASS(BlueprintType)
class TRAMSYSTEM_API UTramDisplayConfiguration : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display", meta = (ClampMin = "1"))
	int32 DisplayCount = 12;

	// ~7.5 ft, half of the ~15 ft reference installation diameter (Objective 20). Configurable,
	// not hardcoded elsewhere in the plugin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display", meta = (ClampMin = "0.0"))
	float CircleRadiusCm = 228.6f;

	// Height (Z, cm) at which screens are vertically centered, relative to the observer rig
	// origin. Documented simplifying assumption: screens are modeled as centered at this one
	// height rather than spanning an independently configurable floor-to-ceiling range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display", meta = (ClampMin = "0.0"))
	float ObserverHeightCm = 150.f;

	// Placeholder dimensions for a portrait 4K panel - not measured from the real installation.
	// Used for debug-visualization box size and will drive off-axis frustum math in Phase 6.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display", meta = (ClampMin = "0.0"))
	float DisplayWidthCm = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display", meta = (ClampMin = "0.0"))
	float DisplayHeightCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	TArray<FTramScreenDefinition> Screens;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	TArray<FTramSlotDisplayMapping> SlotMappings;

	// Regenerates Screens (evenly spaced angular positions) and SlotMappings (contiguous
	// chunks of DisplaysPerSlot) from DisplayCount/CircleRadiusCm. Discards any existing
	// per-screen calibration offsets - an explicit reset-to-ideal tool for initial setup or
	// re-planning the installation, not something run implicitly at load or at runtime.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Tram|Display")
	void GenerateDefaultLayout(int32 DisplaysPerSlot = 3);

	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	bool FindScreen(int32 DisplayIndex, FTramScreenDefinition& OutScreen) const;

	// Transform for a given screen relative to the observer rig's origin (location on the
	// circle at ObserverHeightCm, facing inward toward the center, plus calibration offsets).
	// Combine with ATramViewRig::GetSharedObserverTransform() to get a world-space screen
	// transform: ScreenWorld = GetScreenLocalTransform(Index) * ObserverTransform.
	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	FTransform GetScreenLocalTransform(int32 DisplayIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	TArray<int32> GetDisplaysForSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	int32 GetSlotForDisplay(int32 DisplayIndex) const;

	// Circular mean of the given slot's displays' BaseAngularPositionDegrees - a single
	// representative "which way is this slot facing" angle. Used by the simplified
	// single-camera-per-machine dev mode (Objective 27) to orient each machine's preview
	// camera toward its own slice of the circle; returns 0 if the slot has no displays.
	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	float GetSlotCenterAngularOffsetDegrees(int32 SlotIndex) const;

	// Validates internal consistency: every screen's DisplayIndex in range and unique, every
	// slot mapping's display indices assigned to at most one slot. Does not require every
	// display to be covered by a slot (missing riders are expected - Objective 23).
	UFUNCTION(BlueprintCallable, Category = "Tram|Display")
	bool IsConfigurationValid(FString& OutError) const;
};
