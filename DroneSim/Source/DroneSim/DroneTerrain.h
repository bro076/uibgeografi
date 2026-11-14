#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneTerrain.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** How a terrain is coloured and what is scattered on it. */
UENUM(BlueprintType)
enum class EDroneBiome : uint8
{
	Fjord,      // forested fjord sides, a village by the shore
	Glacier,    // ice cap, bare rock, meltwater lakes
	Farmland    // field patchwork, fruit orchards in rows, farm buildings
};

/**
 * A landscape generated at runtime from a real elevation file (Content/Terrain/*.r16,
 * see Tools/fetch_terrain.py). The centre of the file sits at this actor's origin,
 * +X is north and +Y is east, sea level is Z = 0.
 */
UCLASS()
class DRONESIM_API ADroneTerrain : public AActor
{
	GENERATED_BODY()

public:
	ADroneTerrain();

	/** Loads the height file and builds mesh, water and scatter. Returns false if the file is missing or malformed. */
	bool Build(const FString& FileName, float InSpacingM, EDroneBiome InBiome);

	/** Ground height in metres above sea level at a world position given in metres from the centre. */
	float GetHeightM(double NorthM, double EastM) const;

	/** Distance from the centre to the edge of the terrain, metres. */
	float GetHalfExtentM() const { return SpacingM * (GridSize - 1) * 0.5f; }

	/**
	 * Material for the ground. If unset, /Game/DroneSim/M_DroneTerrain is used when it exists (built by
	 * Tools/setup_realistic_assets.py), otherwise the engine's plain vertex colour material.
	 */
	UPROPERTY(EditAnywhere, Category = "Terrain") TObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Flip this if the ground is invisible from above (triangle winding convention). */
	UPROPERTY(EditAnywhere, Category = "Terrain") bool bFlipWinding = false;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Terrain") TObjectPtr<UProceduralMeshComponent> Ground;
	UPROPERTY(VisibleAnywhere, Category = "Terrain") TObjectPtr<UStaticMeshComponent> Water;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Conifers;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BroadleafCrowns;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> OrchardCrowns;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Trunks;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Houses;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Barns;

	// Real tree models, loaded from /Game/DroneSim/Trees when the setup script has imported them.
	UPROPERTY() TObjectPtr<UStaticMesh> ConiferMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> BroadleafMesh;

private:
	enum class EGround : uint8 { Sea, Shore, Cultivated, Forest, Heath, HighRock, Cliff, Ice, Lake };

	UHierarchicalInstancedStaticMeshComponent* AddScatter(const TCHAR* Name, UStaticMesh* Mesh);
	float HeightAtCell(int32 Row, int32 Col) const;
	void Analyse(int32 Row, int32 Col, float& OutSlopeDeg, float& OutRoughness, FVector& OutNormal) const;
	EGround Classify(double NorthM, double EastM, float HeightM, float SlopeDeg, float Roughness) const;
	EGround ClassifyAt(double NorthM, double EastM, float* OutHeightM = nullptr, float* OutSlopeDeg = nullptr) const;
	/** Flat colour for the fallback material, plus blend weights for the layered material (see NumLayers). */
	FLinearColor Shade(EGround Type, double NorthM, double EastM, float HeightM, float SlopeDeg, float* OutWeights) const;
	FTransform TreeTransform(const UStaticMesh* RealMesh, double NorthM, double EastM, float GroundM, float HeightM, float WidthM, float YawDeg) const;
	bool IsOrchardCell(double NorthM, double EastM) const;
	void BuildScatter();

	static constexpr int32 GridSize = 513;
	// Layer weights, two per UV channel: grass, forest floor, heath, snow, soil, hay, blue ice, lake. The rest is rock.
	static constexpr int32 NumLayers = 8;
	TArray<float> Heights;   // metres, row 0 = north edge
	float SpacingM = 10.f;
	EDroneBiome Biome = EDroneBiome::Fjord;
};
