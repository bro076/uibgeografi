#include "DroneRaceCourse.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ADroneRaceCourse::ADroneRaceCourse()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

	GateBars = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GateBars"));
	GateBars->SetupAttachment(RootComponent);
	GateBars->SetStaticMesh(CubeFinder.Object);
	GateBars->SetCollisionProfileName(TEXT("BlockAll"));

	LandingBoxes = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LandingBoxes"));
	LandingBoxes->SetupAttachment(RootComponent);
	LandingBoxes->SetStaticMesh(CubeFinder.Object);
	LandingBoxes->SetCollisionProfileName(TEXT("BlockAll"));
}

void ADroneRaceCourse::Build(const TArray<FDroneRacePoint>& Points)
{
	GateBars->ClearInstances();
	LandingBoxes->ClearInstances();

	// The engine cube is 100 cm, so a scale is a size in metres.
	const float Span = GateOpeningM + GateBarM * 2.f;
	const float Offset = (GateOpeningM + GateBarM) * 50.f;   // cm from the gate centre to the middle of a bar

	for (const FDroneRacePoint& Point : Points)
	{
		if (Point.bLanding)
		{
			// Location is the top face; the cube is centred half its size below.
			LandingBoxes->AddInstance(FTransform(FRotator(0.f, Point.YawDeg, 0.f), Point.Location - FVector(0.0, 0.0, LandingBoxM * 50.0), FVector(LandingBoxM)));
			continue;
		}

		// A square frame standing across the direction of travel (local X): bars lie in the local Y-Z plane.
		const FTransform Gate(FRotator(0.f, Point.YawDeg, 0.f), Point.Location);
		const FTransform Bars[4] =
		{
			FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, Offset), FVector(GateBarM, Span, GateBarM)),     // top
			FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -Offset), FVector(GateBarM, Span, GateBarM)),    // bottom
			FTransform(FRotator::ZeroRotator, FVector(0.f, Offset, 0.f), FVector(GateBarM, GateBarM, Span)),     // right
			FTransform(FRotator::ZeroRotator, FVector(0.f, -Offset, 0.f), FVector(GateBarM, GateBarM, Span)),    // left
		};
		for (const FTransform& Bar : Bars)
		{
			GateBars->AddInstance(Bar * Gate);
		}
	}

	if (UMaterialInstanceDynamic* Material = GateBars->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.28f, 0.02f));   // signal orange
	}
	if (UMaterialInstanceDynamic* Material = LandingBoxes->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.02f));   // yellow
	}
}
