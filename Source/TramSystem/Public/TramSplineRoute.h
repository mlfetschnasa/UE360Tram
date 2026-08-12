// Pure route data: an Unreal spline plus per-segment speed metadata. Knows nothing about
// movement simulation, networking, or rendering - see "Keep Systems Separated".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TramTypes.h"
#include "TramSplineRoute.generated.h"

class USplineComponent;

UCLASS(BlueprintType)
class TRAMSYSTEM_API ATramSplineRoute : public AActor
{
	GENERATED_BODY()

public:
	ATramSplineRoute();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tram|Route")
	TObjectPtr<USplineComponent> RouteSpline;

	// Identifies this route for FTramMotionState.RouteId validation.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Route")
	FName RouteId = TEXT("DefaultRoute");

	// Used for any segment whose FTramSplinePointMetadata::TargetSpeedCms is negative.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Route", meta = (ClampMin = "0.0"))
	float DefaultTargetSpeedCms = 300.f;

	// Parallel array to the spline's points; index N describes the segment N -> N+1 (or
	// N -> 0 for the closing segment of a closed loop). Kept in sync with the spline's point
	// count automatically in-editor via OnConstruction/PostEditChangeProperty.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tram|Route")
	TArray<FTramSplinePointMetadata> PointSpeeds;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	float GetSplineLengthCm() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	int32 GetNumSplinePoints() const;

	// Number of travelable segments: NumPoints for a closed loop, NumPoints-1 for an open route.
	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	int32 GetNumSegments() const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	bool IsClosedLoop() const;

	// Target speed (cm/s) for travelling from spline point SegmentIndex to SegmentIndex+1.
	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	float GetSegmentTargetSpeedCms(int32 SegmentIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	float GetSegmentStartDistanceCm(int32 SegmentIndex) const;

	// Distance at which SegmentIndex ends: the start of the next segment, or the spline
	// length for the final segment.
	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	float GetSegmentEndDistanceCm(int32 SegmentIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	int32 GetSegmentIndexAtDistance(float DistanceCm) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	FTransform GetTransformAtDistanceCm(float DistanceCm) const;

	// Wraps (closed loop) or clamps (open route) a distance into the valid [0, Length] range.
	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	float NormalizeDistanceCm(float DistanceCm) const;

	UFUNCTION(BlueprintCallable, Category = "Tram|Route")
	bool IsRouteValid() const;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void SyncPointSpeedsArraySize();
};
