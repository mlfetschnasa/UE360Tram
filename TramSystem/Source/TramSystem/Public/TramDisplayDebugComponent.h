// Debug visualization of the virtual display circle (Objective 25). Purely a local rendering
// aid: it only reads already-shared state (ATramViewRig's evaluation, UTramDisplayConfiguration's
// geometry) and draws from it - it owns no simulation state itself and is not replicated, so it
// can be freely added/removed per machine without affecting synchronization.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TramDisplayDebugComponent.generated.h"

class ATramViewRig;
class UTramDisplayConfiguration;

UCLASS(ClassGroup = (Tram), meta = (BlueprintSpawnableComponent))
class TRAMSYSTEM_API UTramDisplayDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTramDisplayDebugComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tram|Debug")
	bool bDrawDebugVisualization = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|Debug")
	TObjectPtr<UTramDisplayConfiguration> DisplayConfiguration;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Tram|Debug")
	TObjectPtr<ATramViewRig> ViewRig;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
