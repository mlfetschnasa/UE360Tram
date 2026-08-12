#include "TramSplineRoute.h"
#include "Components/SplineComponent.h"

ATramSplineRoute::ATramSplineRoute()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	RootComponent = RouteSpline;
}

void ATramSplineRoute::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncPointSpeedsArraySize();
}

#if WITH_EDITOR
void ATramSplineRoute::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncPointSpeedsArraySize();
}
#endif

void ATramSplineRoute::SyncPointSpeedsArraySize()
{
	if (!RouteSpline)
	{
		return;
	}

	const int32 NumPoints = RouteSpline->GetNumberOfSplinePoints();
	if (PointSpeeds.Num() != NumPoints)
	{
		PointSpeeds.SetNum(NumPoints);
	}
}

float ATramSplineRoute::GetSplineLengthCm() const
{
	return RouteSpline ? RouteSpline->GetSplineLength() : 0.f;
}

int32 ATramSplineRoute::GetNumSplinePoints() const
{
	return RouteSpline ? RouteSpline->GetNumberOfSplinePoints() : 0;
}

bool ATramSplineRoute::IsClosedLoop() const
{
	return RouteSpline && RouteSpline->IsClosedLoop();
}

int32 ATramSplineRoute::GetNumSegments() const
{
	const int32 NumPoints = GetNumSplinePoints();
	if (NumPoints < 2)
	{
		return 0;
	}
	return IsClosedLoop() ? NumPoints : (NumPoints - 1);
}

float ATramSplineRoute::GetSegmentTargetSpeedCms(int32 SegmentIndex) const
{
	if (PointSpeeds.IsValidIndex(SegmentIndex) && PointSpeeds[SegmentIndex].TargetSpeedCms >= 0.f)
	{
		return PointSpeeds[SegmentIndex].TargetSpeedCms;
	}
	return DefaultTargetSpeedCms;
}

float ATramSplineRoute::GetSegmentStartDistanceCm(int32 SegmentIndex) const
{
	if (!RouteSpline)
	{
		return 0.f;
	}
	const int32 NumPoints = GetNumSplinePoints();
	const int32 ClampedIndex = FMath::Clamp(SegmentIndex, 0, FMath::Max(0, NumPoints - 1));
	return RouteSpline->GetDistanceAlongSplineAtSplinePoint(ClampedIndex);
}

float ATramSplineRoute::GetSegmentEndDistanceCm(int32 SegmentIndex) const
{
	if (!RouteSpline)
	{
		return 0.f;
	}
	const int32 NumPoints = GetNumSplinePoints();
	const int32 NextIndex = SegmentIndex + 1;
	if (NextIndex < NumPoints)
	{
		return RouteSpline->GetDistanceAlongSplineAtSplinePoint(NextIndex);
	}
	// Final segment: for an open route this is the last point (== spline length); for a
	// closed loop's wrap-around segment this is also the spline length.
	return GetSplineLengthCm();
}

int32 ATramSplineRoute::GetSegmentIndexAtDistance(float DistanceCm) const
{
	const int32 NumSegments = GetNumSegments();
	if (NumSegments <= 0)
	{
		return 0;
	}

	const float Distance = NormalizeDistanceCm(DistanceCm);
	for (int32 Segment = 0; Segment < NumSegments - 1; ++Segment)
	{
		if (Distance < GetSegmentEndDistanceCm(Segment))
		{
			return Segment;
		}
	}
	return NumSegments - 1;
}

FTransform ATramSplineRoute::GetTransformAtDistanceCm(float DistanceCm) const
{
	if (!RouteSpline)
	{
		return FTransform::Identity;
	}
	const float Distance = NormalizeDistanceCm(DistanceCm);
	return RouteSpline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World, true);
}

float ATramSplineRoute::NormalizeDistanceCm(float DistanceCm) const
{
	const float Length = GetSplineLengthCm();
	if (Length <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	if (IsClosedLoop())
	{
		float Wrapped = FMath::Fmod(DistanceCm, Length);
		if (Wrapped < 0.f)
		{
			Wrapped += Length;
		}
		return Wrapped;
	}

	return FMath::Clamp(DistanceCm, 0.f, Length);
}

bool ATramSplineRoute::IsRouteValid() const
{
	return RouteSpline != nullptr
		&& RouteSpline->GetNumberOfSplinePoints() >= 2
		&& GetSplineLengthCm() > UE_KINDA_SMALL_NUMBER;
}
