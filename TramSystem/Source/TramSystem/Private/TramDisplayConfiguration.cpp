#include "TramDisplayConfiguration.h"
#include "TramSystem.h"

void UTramDisplayConfiguration::GenerateDefaultLayoutButton()
{
	GenerateDefaultLayout(3);
}

void UTramDisplayConfiguration::GenerateDefaultLayout(int32 DisplaysPerSlot)
{
	Screens.Reset();
	SlotMappings.Reset();

	if (DisplayCount <= 0 || DisplaysPerSlot <= 0)
	{
		UE_LOG(LogTramSystem, Warning, TEXT("GenerateDefaultLayout on %s: invalid DisplayCount=%d or DisplaysPerSlot=%d"),
			*GetNameSafe(this), DisplayCount, DisplaysPerSlot);
		return;
	}

	const float AngularStepDegrees = 360.f / static_cast<float>(DisplayCount);
	for (int32 Index = 0; Index < DisplayCount; ++Index)
	{
		FTramScreenDefinition Screen;
		Screen.DisplayIndex = Index;
		Screen.BaseAngularPositionDegrees = AngularStepDegrees * static_cast<float>(Index);
		Screens.Add(Screen);
	}

	const int32 NumSlots = FMath::DivideAndRoundUp(DisplayCount, DisplaysPerSlot);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		FTramSlotDisplayMapping Mapping;
		Mapping.SlotIndex = Slot;
		for (int32 i = 0; i < DisplaysPerSlot; ++i)
		{
			const int32 DisplayIndex = Slot * DisplaysPerSlot + i;
			if (DisplayIndex < DisplayCount)
			{
				Mapping.DisplayIndices.Add(DisplayIndex);
			}
		}
		SlotMappings.Add(Mapping);
	}

	UE_LOG(LogTramSystem, Log, TEXT("Generated default display layout on %s: %d displays, %d slots x %d displays/slot"),
		*GetNameSafe(this), DisplayCount, NumSlots, DisplaysPerSlot);
}

bool UTramDisplayConfiguration::FindScreen(int32 DisplayIndex, FTramScreenDefinition& OutScreen) const
{
	for (const FTramScreenDefinition& Screen : Screens)
	{
		if (Screen.DisplayIndex == DisplayIndex)
		{
			OutScreen = Screen;
			return true;
		}
	}
	return false;
}

FTransform UTramDisplayConfiguration::GetScreenLocalTransform(int32 DisplayIndex) const
{
	FTramScreenDefinition Screen;
	if (!FindScreen(DisplayIndex, Screen))
	{
		return FTransform::Identity;
	}

	const float ThetaDegrees = Screen.BaseAngularPositionDegrees;
	const float ThetaRadians = FMath::DegreesToRadians(ThetaDegrees);

	FVector BasePosition(CircleRadiusCm * FMath::Cos(ThetaRadians), CircleRadiusCm * FMath::Sin(ThetaRadians), ObserverHeightCm);
	BasePosition += Screen.CalibrationPositionOffsetCm;

	// Screens are tangent to the circle, facing inward toward the observer at the center:
	// the outward radial direction at ThetaDegrees has yaw = ThetaDegrees, so inward-facing
	// is that + 180.
	const FRotator BaseRotation(Screen.CalibrationPitchOffsetDegrees, ThetaDegrees + 180.f + Screen.CalibrationYawOffsetDegrees, Screen.CalibrationRollOffsetDegrees);

	return FTransform(BaseRotation, BasePosition);
}

TArray<int32> UTramDisplayConfiguration::GetDisplaysForSlot(int32 SlotIndex) const
{
	for (const FTramSlotDisplayMapping& Mapping : SlotMappings)
	{
		if (Mapping.SlotIndex == SlotIndex)
		{
			return Mapping.DisplayIndices;
		}
	}
	return TArray<int32>();
}

float UTramDisplayConfiguration::GetSlotCenterAngularOffsetDegrees(int32 SlotIndex) const
{
	// Circular mean (not a naive arithmetic average, which breaks near the 0/360 wraparound -
	// e.g. displays at 350 and 10 degrees should average to 0, not 180).
	double SumSin = 0.0;
	double SumCos = 0.0;
	int32 Count = 0;

	for (int32 DisplayIndex : GetDisplaysForSlot(SlotIndex))
	{
		FTramScreenDefinition Screen;
		if (FindScreen(DisplayIndex, Screen))
		{
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Screen.BaseAngularPositionDegrees));
			SumSin += FMath::Sin(Radians);
			SumCos += FMath::Cos(Radians);
			++Count;
		}
	}

	if (Count == 0)
	{
		return 0.f;
	}

	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(SumSin, SumCos)));
}

int32 UTramDisplayConfiguration::GetSlotForDisplay(int32 DisplayIndex) const
{
	for (const FTramSlotDisplayMapping& Mapping : SlotMappings)
	{
		if (Mapping.DisplayIndices.Contains(DisplayIndex))
		{
			return Mapping.SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool UTramDisplayConfiguration::IsConfigurationValid(FString& OutError) const
{
	if (DisplayCount <= 0)
	{
		OutError = TEXT("DisplayCount must be > 0");
		return false;
	}
	if (CircleRadiusCm <= 0.f)
	{
		OutError = TEXT("CircleRadiusCm must be > 0");
		return false;
	}

	TSet<int32> SeenScreens;
	for (const FTramScreenDefinition& Screen : Screens)
	{
		if (Screen.DisplayIndex < 0 || Screen.DisplayIndex >= DisplayCount)
		{
			OutError = FString::Printf(TEXT("Screen definition has out-of-range DisplayIndex %d (DisplayCount=%d)"), Screen.DisplayIndex, DisplayCount);
			return false;
		}
		bool bAlreadySeen = false;
		SeenScreens.Add(Screen.DisplayIndex, &bAlreadySeen);
		if (bAlreadySeen)
		{
			OutError = FString::Printf(TEXT("Duplicate screen definition for DisplayIndex %d"), Screen.DisplayIndex);
			return false;
		}
	}

	TSet<int32> AssignedDisplays;
	for (const FTramSlotDisplayMapping& Mapping : SlotMappings)
	{
		for (int32 DisplayIndex : Mapping.DisplayIndices)
		{
			bool bAlreadyAssigned = false;
			AssignedDisplays.Add(DisplayIndex, &bAlreadyAssigned);
			if (bAlreadyAssigned)
			{
				OutError = FString::Printf(TEXT("Display %d is assigned to more than one slot mapping"), DisplayIndex);
				return false;
			}
		}
	}

	OutError.Reset();
	return true;
}

void UTramDisplayConfiguration::LogAllScreenTransforms() const
{
	UE_LOG(LogTramSystem, Log, TEXT("=== %s screen transforms (relative to observer rig origin, cm/degrees) ==="), *GetNameSafe(this));

	for (const FTramScreenDefinition& Screen : Screens)
	{
		const FTransform LocalTransform = GetScreenLocalTransform(Screen.DisplayIndex);
		const FVector Location = LocalTransform.GetLocation();
		const FRotator Rotation = LocalTransform.GetRotation().Rotator();
		const int32 OwningSlot = GetSlotForDisplay(Screen.DisplayIndex);

		UE_LOG(LogTramSystem, Log, TEXT("Display %d (Slot %d): Location=(X=%.2f,Y=%.2f,Z=%.2f) Rotation=(Pitch=%.2f,Yaw=%.2f,Roll=%.2f) Size=(W=%.2f,H=%.2f)"),
			Screen.DisplayIndex, OwningSlot, Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll, DisplayWidthCm, DisplayHeightCm);
	}

	UE_LOG(LogTramSystem, Log, TEXT("=== end screen transforms (%d screens) ==="), Screens.Num());
}
