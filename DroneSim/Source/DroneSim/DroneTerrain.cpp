#include "DroneTerrain.h"

#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogDroneTerrain, Log, All);

namespace
{
	const float SeaLevelM = 0.2f;        // anything at or below this in the data is water
	const float SeabedM = -4.f;
	const float FieldCellM = 64.f;       // size of one field / orchard parcel
	const float ScatterClearM = 14.f;    // keep the take-off point free of trees and houses

	/** Stable pseudo-random 0..1 for an integer cell. */
	float CellRandom(int32 A, int32 B, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u ^ Salt * 83492791u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return static_cast<float>(H & 0xFFFFu) / 65535.f;
	}

	float Noise(double NorthM, double EastM, double WavelengthM)
	{
		return FMath::PerlinNoise2D(FVector2D(NorthM / WavelengthM + 13.7, EastM / WavelengthM - 4.2));
	}

	void SetScatterColor(UPrimitiveComponent* Component, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* Material = Component ? Component->CreateDynamicMaterialInstance(0) : nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);   // parameter of the engine's BasicShapeMaterial
		}
	}
}

ADroneTerrain::ADroneTerrain()
{
	PrimaryActorTick.bCanEverTick = false;

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->bUseAsyncCooking = false;   // collision must exist before the drone is placed
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	RootComponent = Ground;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(RootComponent);
	Water->SetStaticMesh(PlaneFinder.Object);
	Water->SetCollisionProfileName(TEXT("BlockAll"));   // flying into the fjord is a crash
	Water->SetVisibility(false);

	Conifers = AddScatter(TEXT("Conifers"), ConeFinder.Object);
	BroadleafCrowns = AddScatter(TEXT("BroadleafCrowns"), SphereFinder.Object);
	OrchardCrowns = AddScatter(TEXT("OrchardCrowns"), SphereFinder.Object);
	Trunks = AddScatter(TEXT("Trunks"), CylinderFinder.Object);
	Houses = AddScatter(TEXT("Houses"), CubeFinder.Object);
	Barns = AddScatter(TEXT("Barns"), CubeFinder.Object);
}

UHierarchicalInstancedStaticMeshComponent* ADroneTerrain::AddScatter(const TCHAR* Name, UStaticMesh* Mesh)
{
	UHierarchicalInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
	Component->SetupAttachment(RootComponent);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionProfileName(TEXT("BlockAll"));
	Component->SetCullDistances(120000, 160000);   // fade out between 1.2 and 1.6 km
	return Component;
}

float ADroneTerrain::HeightAtCell(int32 Row, int32 Col) const
{
	Row = FMath::Clamp(Row, 0, GridSize - 1);
	Col = FMath::Clamp(Col, 0, GridSize - 1);
	return Heights[Row * GridSize + Col];
}

float ADroneTerrain::GetHeightM(double NorthM, double EastM) const
{
	if (Heights.Num() == 0)
	{
		return 0.f;
	}
	const double Half = (GridSize - 1) * 0.5;
	const double RowF = FMath::Clamp<double>(Half - NorthM / SpacingM, 0.0, GridSize - 1.001);
	const double ColF = FMath::Clamp<double>(Half + EastM / SpacingM, 0.0, GridSize - 1.001);
	const int32 Row = static_cast<int32>(RowF);
	const int32 Col = static_cast<int32>(ColF);
	const float FracR = static_cast<float>(RowF - Row);
	const float FracC = static_cast<float>(ColF - Col);
	const float Top = FMath::Lerp(HeightAtCell(Row, Col), HeightAtCell(Row, Col + 1), FracC);
	const float Bottom = FMath::Lerp(HeightAtCell(Row + 1, Col), HeightAtCell(Row + 1, Col + 1), FracC);
	return FMath::Lerp(Top, Bottom, FracR);
}

void ADroneTerrain::Analyse(int32 Row, int32 Col, float& OutSlopeDeg, float& OutRoughness, FVector& OutNormal) const
{
	// Row index grows southward, so north is Row - 1.
	const float SlopeNorth = (HeightAtCell(Row - 1, Col) - HeightAtCell(Row + 1, Col)) / (2.f * SpacingM);
	const float SlopeEast = (HeightAtCell(Row, Col + 1) - HeightAtCell(Row, Col - 1)) / (2.f * SpacingM);
	OutSlopeDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(SlopeNorth * SlopeNorth + SlopeEast * SlopeEast)));
	OutNormal = FVector(-SlopeNorth, -SlopeEast, 1.f).GetSafeNormal();

	// How far this point sticks out from its surroundings: near zero on ice and water, large on broken rock.
	const float Around = (HeightAtCell(Row - 2, Col) + HeightAtCell(Row + 2, Col) + HeightAtCell(Row, Col - 2) + HeightAtCell(Row, Col + 2)) * 0.25f;
	OutRoughness = FMath::Abs(HeightAtCell(Row, Col) - Around);
}

ADroneTerrain::EGround ADroneTerrain::Classify(double NorthM, double EastM, float HeightM, float SlopeDeg, float Roughness) const
{
	if (Biome == EDroneBiome::Glacier)
	{
		if (SlopeDeg < 0.4f && Roughness < 0.05f && HeightM < 1120.f) { return EGround::Lake; }
		if (HeightM > 1150.f && Roughness < 0.35f && SlopeDeg < 28.f) { return EGround::Ice; }
		return SlopeDeg > 42.f ? EGround::Cliff : EGround::HighRock;
	}

	if (HeightM <= SeaLevelM) { return EGround::Sea; }
	if (HeightM < 1.5f) { return EGround::Shore; }
	if (SlopeDeg > 44.f) { return EGround::Cliff; }

	const float TreeLine = 850.f + 60.f * Noise(NorthM, EastM, 900.0);
	if (HeightM < TreeLine)
	{
		// Farmland keeps cultivating steeper and higher ground, as the Hardanger fruit farms do.
		const float MaxSlope = Biome == EDroneBiome::Farmland ? 24.f : 14.f;
		const float MaxHeight = Biome == EDroneBiome::Farmland ? 300.f : 160.f;
		return (SlopeDeg < MaxSlope && HeightM < MaxHeight) ? EGround::Cultivated : EGround::Forest;
	}
	return HeightM < TreeLine + 300.f ? EGround::Heath : EGround::HighRock;
}

ADroneTerrain::EGround ADroneTerrain::ClassifyAt(double NorthM, double EastM, float* OutHeightM, float* OutSlopeDeg) const
{
	const double Half = (GridSize - 1) * 0.5;
	const int32 Row = FMath::RoundToInt32(Half - NorthM / SpacingM);
	const int32 Col = FMath::RoundToInt32(Half + EastM / SpacingM);
	float SlopeDeg = 0.f, Roughness = 0.f;
	FVector Normal;
	Analyse(Row, Col, SlopeDeg, Roughness, Normal);
	const float HeightM = HeightAtCell(Row, Col);
	if (OutHeightM) { *OutHeightM = GetHeightM(NorthM, EastM); }
	if (OutSlopeDeg) { *OutSlopeDeg = SlopeDeg; }
	return Classify(NorthM, EastM, HeightM, SlopeDeg, Roughness);
}

bool ADroneTerrain::IsOrchardCell(double NorthM, double EastM) const
{
	if (Biome != EDroneBiome::Farmland)
	{
		return false;
	}
	const int32 CellN = FMath::FloorToInt32(NorthM / FieldCellM);
	const int32 CellE = FMath::FloorToInt32(EastM / FieldCellM);
	return CellRandom(CellN, CellE, 7u) < 0.4f;
}

FLinearColor ADroneTerrain::ColorFor(EGround Type, double NorthM, double EastM, float HeightM, float SlopeDeg) const
{
	const float Broad = Noise(NorthM, EastM, 260.0);   // -1..1
	const float Fine = Noise(NorthM, EastM, 31.0);
	FLinearColor Color;

	switch (Type)
	{
	case EGround::Sea:
		Color = FLinearColor(0.05f, 0.07f, 0.06f);
		break;
	case EGround::Shore:
		Color = FLinearColor(0.30f, 0.28f, 0.24f);
		break;
	case EGround::Lake:
		return FLinearColor(0.07f, 0.20f, 0.24f);   // meltwater
	case EGround::Ice:
	{
		// Clean snow on the flat cap, blue ice and crevasse bands where the glacier steepens and flows.
		Color = FLinearColor(0.88f, 0.91f, 0.95f);
		const float Flow = FMath::Clamp((SlopeDeg - 7.f) / 12.f, 0.f, 1.f);
		const float Band = FMath::Abs(FMath::Sin(static_cast<float>((NorthM * 0.6 + EastM * 0.8) / 23.0) + Broad * 3.f));
		Color = FMath::Lerp(Color, FLinearColor(0.40f, 0.66f, 0.84f), Flow * (0.35f + 0.65f * FMath::Pow(Band, 6.f)));
		break;
	}
	case EGround::Cultivated:
	{
		if (Biome == EDroneBiome::Farmland)
		{
			// One colour per parcel: pasture, hay meadow, ploughed soil, or the grass under an orchard.
			const int32 CellN = FMath::FloorToInt32(NorthM / FieldCellM);
			const int32 CellE = FMath::FloorToInt32(EastM / FieldCellM);
			const float Pick = CellRandom(CellN, CellE, 3u);
			if (IsOrchardCell(NorthM, EastM)) { Color = FLinearColor(0.11f, 0.24f, 0.06f); }
			else if (Pick < 0.5f) { Color = FLinearColor(0.20f, 0.36f, 0.08f); }
			else if (Pick < 0.8f) { Color = FLinearColor(0.42f, 0.40f, 0.13f); }
			else { Color = FLinearColor(0.20f, 0.13f, 0.08f); }
		}
		else
		{
			Color = FLinearColor(0.18f, 0.32f, 0.08f);
		}
		break;
	}
	case EGround::Forest:
		Color = FMath::Lerp(FLinearColor(0.04f, 0.10f, 0.035f), FLinearColor(0.09f, 0.17f, 0.05f), Broad * 0.5f + 0.5f);
		break;
	case EGround::Heath:
		Color = FMath::Lerp(FLinearColor(0.20f, 0.20f, 0.09f), FLinearColor(0.26f, 0.22f, 0.13f), Broad * 0.5f + 0.5f);
		break;
	case EGround::Cliff:
		Color = FLinearColor(0.17f, 0.17f, 0.18f);
		break;
	case EGround::HighRock:
	default:
	{
		Color = FLinearColor(0.23f, 0.22f, 0.21f);
		if (Biome == EDroneBiome::Glacier && HeightM < 980.f)
		{
			Color = FMath::Lerp(Color, FLinearColor(0.19f, 0.21f, 0.11f), 0.6f);   // moss and lichen lower down
		}
		const float SnowLine = Biome == EDroneBiome::Glacier ? 1050.f : 1200.f;
		if (HeightM > SnowLine && SlopeDeg < 30.f && Broad + Fine * 0.5f > 0.1f)
		{
			Color = FLinearColor(0.86f, 0.89f, 0.93f);   // lingering snow patches
		}
		break;
	}
	}

	return Color * (0.92f + 0.08f * Fine);
}

bool ADroneTerrain::Build(const FString& FileName, float InSpacingM, EDroneBiome InBiome)
{
	SpacingM = FMath::Max(InSpacingM, 0.5f);
	Biome = InBiome;

	const FString Path = FPaths::ProjectContentDir() / TEXT("Terrain") / FileName;
	TArray<uint8> Bytes;
	const int32 NumSamples = GridSize * GridSize;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != NumSamples * 2)
	{
		UE_LOG(LogDroneTerrain, Error, TEXT("Terrain file missing or not %dx%d 16-bit: %s"), GridSize, GridSize, *Path);
		return false;
	}

	// File format: little-endian uint16, value = (metres + 100) * 10.
	Heights.SetNumUninitialized(NumSamples);
	bool bHasSea = false;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const uint16 Raw = static_cast<uint16>(Bytes[i * 2]) | (static_cast<uint16>(Bytes[i * 2 + 1]) << 8);
		Heights[i] = Raw / 10.f - 100.f;
		bHasSea |= Biome != EDroneBiome::Glacier && Heights[i] <= SeaLevelM;
	}

	// ---- Ground mesh
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<int32> Triangles;
	Vertices.Reserve(NumSamples);
	Normals.Reserve(NumSamples);
	UVs.Reserve(NumSamples);
	Colors.Reserve(NumSamples);
	Triangles.Reserve((GridSize - 1) * (GridSize - 1) * 6);

	const double Half = (GridSize - 1) * 0.5;
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Col = 0; Col < GridSize; ++Col)
		{
			const double NorthM = (Half - Row) * SpacingM;
			const double EastM = (Col - Half) * SpacingM;
			const float HeightM = HeightAtCell(Row, Col);

			float SlopeDeg = 0.f, Roughness = 0.f;
			FVector Normal;
			Analyse(Row, Col, SlopeDeg, Roughness, Normal);
			const EGround Type = Classify(NorthM, EastM, HeightM, SlopeDeg, Roughness);

			// Push the sea floor down so the shoreline is a clean intersection with the water plane.
			const float MeshHeightM = Type == EGround::Sea ? SeabedM : HeightM;
			Vertices.Add(FVector(NorthM * 100.0, EastM * 100.0, MeshHeightM * 100.0));
			Normals.Add(Normal);
			UVs.Add(FVector2D(Col / 8.0, Row / 8.0));
			Colors.Add(ColorFor(Type, NorthM, EastM, HeightM, SlopeDeg));
		}
	}

	for (int32 Row = 0; Row < GridSize - 1; ++Row)
	{
		for (int32 Col = 0; Col < GridSize - 1; ++Col)
		{
			const int32 NW = Row * GridSize + Col;
			const int32 NE = NW + 1;
			const int32 SW = NW + GridSize;
			const int32 SE = SW + 1;
			// Unreal's front face: (P1 - P2) x (P0 - P2) points at the viewer. These face up (+Z).
			const int32 Quad[6] = { SW, SE, NW, SE, NE, NW };
			for (int32 i = 0; i < 6; i += 3)
			{
				Triangles.Add(Quad[i]);
				Triangles.Add(bFlipWinding ? Quad[i + 2] : Quad[i + 1]);
				Triangles.Add(bFlipWinding ? Quad[i + 1] : Quad[i + 2]);
			}
		}
	}

	Ground->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, TArray<FProcMeshTangent>(), true);
	UMaterialInterface* Material = TerrainMaterial.Get();
	if (!Material && GEngine)
	{
		Material = static_cast<UMaterialInterface*>(GEngine->VertexColorMaterial);
	}
	if (Material)
	{
		Ground->SetMaterial(0, Material);
	}

	// ---- Sea
	Water->SetVisibility(bHasSea);
	Water->SetCollisionEnabled(bHasSea ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bHasSea)
	{
		const float PlaneScale = GetHalfExtentM() * 6.f;   // basic plane is 1 m across at scale 1; reach well past the terrain edge
		Water->SetRelativeScale3D(FVector(PlaneScale, PlaneScale, 1.f));
		Water->SetRelativeLocation(FVector::ZeroVector);
		SetScatterColor(Water, FLinearColor(0.012f, 0.045f, 0.06f));
	}

	BuildScatter();

	UE_LOG(LogDroneTerrain, Log, TEXT("Built terrain %s: %.1f km square, %d trees, %d buildings"), *FileName,
		GetHalfExtentM() * 2.f / 1000.f,
		Conifers->GetInstanceCount() + BroadleafCrowns->GetInstanceCount() + OrchardCrowns->GetInstanceCount(),
		Houses->GetInstanceCount() + Barns->GetInstanceCount());
	return true;
}

void ADroneTerrain::BuildScatter()
{
	TArray<FTransform> ConiferT, BroadleafT, OrchardT, TrunkT, HouseT, BarnT;
	FRandomStream Random(static_cast<int32>(Biome) * 7919 + 1234);

	// Basic shapes are 100 cm across and centred on their origin, so scale = size in metres
	// and the centre goes half the height above the ground.
	auto Place = [](TArray<FTransform>& Out, double NorthM, double EastM, float GroundM, float CentreUpM, const FVector& SizeM, float YawDeg)
	{
		Out.Add(FTransform(FRotator(0.f, YawDeg, 0.f), FVector(NorthM * 100.0, EastM * 100.0, (GroundM + CentreUpM) * 100.0), SizeM));
	};
	auto IsClear = [](double NorthM, double EastM)
	{
		return NorthM * NorthM + EastM * EastM > ScatterClearM * ScatterClearM;
	};
	const float Edge = GetHalfExtentM() - 30.f;

	if (Biome != EDroneBiome::Glacier)
	{
		// ---- Buildings: a village by the shore (fjord) or scattered farmsteads (farmland)
		const float BuildingRange = FMath::Min(700.f, Edge);
		const float BuildingGrid = 42.f;
		const float BuildingChance = Biome == EDroneBiome::Fjord ? 0.3f : 0.12f;
		for (float N = -BuildingRange; N < BuildingRange; N += BuildingGrid)
		{
			for (float E = -BuildingRange; E < BuildingRange; E += BuildingGrid)
			{
				const double NorthM = N + Random.FRandRange(0.f, BuildingGrid * 0.6f);
				const double EastM = E + Random.FRandRange(0.f, BuildingGrid * 0.6f);
				const float Roll = Random.FRand();
				const float Yaw = Random.FRandRange(0.f, 180.f);
				const bool bBarn = Random.FRand() < 0.3f;
				float HeightM = 0.f, SlopeDeg = 0.f;
				if (Roll > BuildingChance || !IsClear(NorthM, EastM)
					|| ClassifyAt(NorthM, EastM, &HeightM, &SlopeDeg) != EGround::Cultivated
					|| SlopeDeg > 13.f || HeightM < 2.5f || HeightM > 170.f || IsOrchardCell(NorthM, EastM))
				{
					continue;
				}
				if (bBarn) { Place(BarnT, NorthM, EastM, HeightM, 2.5f, FVector(16.f, 9.f, 7.f), Yaw); }
				else { Place(HouseT, NorthM, EastM, HeightM, 2.f, FVector(10.f, 7.5f, 5.5f), Yaw); }
			}
		}

		// ---- Forest: spruce and birch on the uncultivated slopes around the flying area
		const float ForestRange = FMath::Min(1000.f, Edge);
		const float ForestGrid = 17.f;
		for (float N = -ForestRange; N < ForestRange && ConiferT.Num() + BroadleafT.Num() < 6000; N += ForestGrid)
		{
			for (float E = -ForestRange; E < ForestRange; E += ForestGrid)
			{
				const double NorthM = N + Random.FRandRange(0.f, ForestGrid);
				const double EastM = E + Random.FRandRange(0.f, ForestGrid);
				const float Size = Random.FRandRange(0.7f, 1.3f);
				const bool bSpruce = Random.FRand() < 0.45f;
				float HeightM = 0.f, SlopeDeg = 0.f;
				if (NorthM * NorthM + EastM * EastM > ForestRange * ForestRange || !IsClear(NorthM, EastM)
					|| ClassifyAt(NorthM, EastM, &HeightM, &SlopeDeg) != EGround::Forest || SlopeDeg > 40.f)
				{
					continue;
				}
				if (bSpruce)
				{
					Place(ConiferT, NorthM, EastM, HeightM, 5.5f * Size, FVector(4.f, 4.f, 11.f) * Size, 0.f);
				}
				else
				{
					Place(BroadleafT, NorthM, EastM, HeightM, 6.f * Size, FVector(5.f, 5.f, 6.f) * Size, 0.f);
					Place(TrunkT, NorthM, EastM, HeightM, 1.8f * Size, FVector(0.35f, 0.35f, 4.f) * Size, 0.f);
				}
			}
		}
	}

	if (Biome == EDroneBiome::Farmland)
	{
		// ---- Orchards: fruit trees in straight rows, 4.5 m between rows and 3.5 m along them
		const float OrchardRange = FMath::Min(380.f, Edge);
		const float RowGap = 4.5f;
		const float TreeGap = 3.5f;
		for (float N = -OrchardRange; N < OrchardRange && OrchardT.Num() < 12000; N += TreeGap)
		{
			for (float E = -OrchardRange; E < OrchardRange; E += RowGap)
			{
				const float Size = Random.FRandRange(0.85f, 1.15f);
				// Leave a tractor track around each parcel.
				const float InCellN = N - FMath::FloorToFloat(N / FieldCellM) * FieldCellM;
				const float InCellE = E - FMath::FloorToFloat(E / FieldCellM) * FieldCellM;
				float HeightM = 0.f, SlopeDeg = 0.f;
				if (InCellN < 5.f || InCellE < 5.f || !IsOrchardCell(N, E) || !IsClear(N, E)
					|| N * N + E * E > OrchardRange * OrchardRange
					|| ClassifyAt(N, E, &HeightM, &SlopeDeg) != EGround::Cultivated || HeightM < 2.5f)
				{
					continue;
				}
				Place(OrchardT, N, E, HeightM, 2.3f * Size, FVector(2.6f, 2.6f, 2.3f) * Size, 0.f);
				Place(TrunkT, N, E, HeightM, 0.7f, FVector(0.16f, 0.16f, 1.6f), 0.f);
			}
		}
	}

	Conifers->AddInstances(ConiferT, false);
	BroadleafCrowns->AddInstances(BroadleafT, false);
	OrchardCrowns->AddInstances(OrchardT, false);
	Trunks->AddInstances(TrunkT, false);
	Houses->AddInstances(HouseT, false);
	Barns->AddInstances(BarnT, false);

	SetScatterColor(Conifers, FLinearColor(0.02f, 0.07f, 0.03f));
	SetScatterColor(BroadleafCrowns, FLinearColor(0.10f, 0.22f, 0.05f));
	SetScatterColor(OrchardCrowns, FLinearColor(0.16f, 0.30f, 0.07f));
	SetScatterColor(Trunks, FLinearColor(0.10f, 0.07f, 0.05f));
	SetScatterColor(Houses, FLinearColor(0.85f, 0.84f, 0.80f));   // white-painted houses
	SetScatterColor(Barns, FLinearColor(0.45f, 0.06f, 0.04f));    // red barns
}
