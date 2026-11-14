#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneRaceCourse.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/** One step of a race: fly through a gate, or land on a small box. */
struct FDroneRacePoint
{
	FVector Location = FVector::ZeroVector;   // cm. Gate: centre of the opening. Landing: centre of the box top.
	float YawDeg = 0.f;                       // direction of travel through a gate
	bool bLanding = false;
};

/** The physical course: square gate frames and landing boxes. Both are solid - hitting a frame is a crash. */
UCLASS()
class DRONESIM_API ADroneRaceCourse : public AActor
{
	GENERATED_BODY()

public:
	ADroneRaceCourse();

	void Build(const TArray<FDroneRacePoint>& Points);

	static constexpr float GateOpeningM = 7.f;      // inner width and height of a gate
	static constexpr float GateBarM = 0.35f;        // thickness of the frame
	static constexpr float LandingBoxM = 1.2f;      // landing boxes are cubes this size

protected:
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GateBars;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LandingBoxes;
};
