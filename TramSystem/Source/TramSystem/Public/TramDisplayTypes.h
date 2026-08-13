// Data-driven physical display geometry types. Deliberately separate from TramTypes.h (tram
// simulation/observer-rig concerns) - see "Keep Systems Separated": the projection system
// should not be entangled with tram simulation types, even though both live in one module.
#pragma once

#include "CoreMinimal.h"
#include "TramDisplayTypes.generated.h"

// One physical display's geometry. Position/orientation are generated parametrically by
// UTramDisplayConfiguration::GenerateDefaultLayout() as a starting point, then this struct's
// calibration fields let an installer fine-tune an individual screen away from the ideal
// without losing the parametric baseline.
USTRUCT(BlueprintType)
struct FTramScreenDefinition
{
	GENERATED_BODY()

	// Global display index (0..DisplayCount-1), independent of which machine/slot renders it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	int32 DisplayIndex = 0;

	// Angular position around the circle in degrees (0 = +X, increasing toward +Y), before
	// calibration offsets. Set by GenerateDefaultLayout(); editable directly for an
	// installation that isn't perfectly regular.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	float BaseAngularPositionDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display|Calibration")
	FVector CalibrationPositionOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display|Calibration")
	float CalibrationYawOffsetDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display|Calibration")
	float CalibrationPitchOffsetDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display|Calibration")
	float CalibrationRollOffsetDegrees = 0.f;
};

// Which global display indices a given machine/view slot renders. A separate, explicit array
// rather than a formula, so an installation's mapping never has to be "exactly evenly
// distributed" (Objective 21).
USTRUCT(BlueprintType)
struct FTramSlotDisplayMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Display")
	TArray<int32> DisplayIndices;
};
