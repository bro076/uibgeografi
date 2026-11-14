#include "DroneSimGameMode.h"

#include "DroneHUD.h"
#include "DronePawn.h"
#include "DroneTerrain.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	struct FDroneScene
	{
		const TCHAR* Name;
		const TCHAR* FileName;     // in Content/Terrain
		float SpacingM;            // distance between height samples
		EDroneBiome Biome;
	};

	// Real elevation data, see Tools/fetch_terrain.py and the README for coordinates.
	const FDroneScene Scenes[] =
	{
		{ TEXT("Jondal - fjord and village"), TEXT("jondal.r16"), 12.f, EDroneBiome::Fjord },
		{ TEXT("Folgefonna - glacier"), TEXT("folgefonna.r16"), 12.f, EDroneBiome::Glacier },
		{ TEXT("Hardanger - farms and orchards"), TEXT("orchards.r16"), 8.f, EDroneBiome::Farmland },
	};
	const int32 NumScenes = UE_ARRAY_COUNT(Scenes);
}

ADroneSimGameMode::ADroneSimGameMode()
{
	DefaultPawnClass = ADronePawn::StaticClass();
	HUDClass = ADroneHUD::StaticClass();
}

void ADroneSimGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A map that brings its own sun is a hand-built environment: leave it alone.
	bBuiltInScenes = !TActorIterator<ADirectionalLight>(World);
	if (!bBuiltInScenes)
	{
		return;
	}

	SpawnSky();

	// Start scene: ?Scene=1 on the map URL, else StartScene in DefaultGame.ini, else 0.
	int32 StartScene = 0;
	if (GConfig)
	{
		GConfig->GetInt(TEXT("DroneSim"), TEXT("StartScene"), StartScene, GGameIni);
	}
	if (UGameplayStatics::HasOption(Options, TEXT("Scene")))
	{
		StartScene = UGameplayStatics::GetIntOption(Options, TEXT("Scene"), StartScene);
	}
	LoadScene(StartScene);
}

void ADroneSimGameMode::SpawnSky()
{
	UWorld* World = GetWorld();

	// Early-afternoon sun from the south-west.
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 300000.0), FRotator(-42.f, 35.f, 0.f)))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Light->SetIntensity(9.f);
			Light->SetAtmosphereSunLight(true);
		}
	}

	World->SpawnActor<ASkyAtmosphere>();
	World->SpawnActor<AVolumetricCloud>();

	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>())
	{
		if (USkyLightComponent* Light = Sky->GetLightComponent())
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->bRealTimeCapture = true;   // light the scene from the atmosphere and clouds
			Light->MarkRenderStateDirty();
		}
	}

	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>())
	{
		if (UExponentialHeightFogComponent* Component = Fog->GetComponent())
		{
			Component->SetFogDensity(0.006f);
			Component->SetVolumetricFog(true);   // soft light shafts and haze in the valleys
		}
	}

	// A light film grade over the whole world.
	if (APostProcessVolume* Grade = World->SpawnActor<APostProcessVolume>())
	{
		Grade->bUnbound = true;
		FPostProcessSettings& Settings = Grade->Settings;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.4f;
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.25f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.3f;
		Settings.bOverride_AutoExposureSpeedUp = true;
		Settings.AutoExposureSpeedUp = 2.f;
		Settings.bOverride_AutoExposureSpeedDown = true;
		Settings.AutoExposureSpeedDown = 1.5f;
	}
}

void ADroneSimGameMode::CycleScene()
{
	if (bBuiltInScenes)
	{
		LoadScene((SceneIndex + 1) % NumScenes);
	}
}

void ADroneSimGameMode::LoadScene(int32 Index)
{
	UWorld* World = GetWorld();
	if (!bBuiltInScenes || !World)
	{
		return;
	}

	SceneIndex = FMath::Clamp(Index, 0, NumScenes - 1);
	const FDroneScene& Scene = Scenes[SceneIndex];

	if (Terrain)
	{
		Terrain->Destroy();
		Terrain = nullptr;
	}
	Terrain = World->SpawnActor<ADroneTerrain>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Terrain && !Terrain->Build(Scene.FileName, Scene.SpacingM, Scene.Biome))
	{
		Terrain->Destroy();
		Terrain = nullptr;
	}

	// Drones that already exist (scene switch during play) move to the new take-off point.
	FVector Spawn;
	float FenceRadiusM = 0.f;
	if (GetSceneSpawn(Spawn, FenceRadiusM))
	{
		for (TActorIterator<ADronePawn> It(World); It; ++It)
		{
			It->SetSpawnPoint(Spawn, FenceRadiusM);
		}
	}
}

FString ADroneSimGameMode::GetSceneName() const
{
	return (bBuiltInScenes && SceneIndex >= 0) ? FString(Scenes[SceneIndex].Name) : FString();
}

bool ADroneSimGameMode::GetSceneSpawn(FVector& OutLocation, float& OutFenceRadiusM) const
{
	if (!Terrain)
	{
		return false;
	}
	// The terrain file is centred on the take-off point.
	OutLocation = FVector(0.0, 0.0, Terrain->GetHeightM(0.0, 0.0) * 100.0);
	OutFenceRadiusM = Terrain->GetHalfExtentM() - 150.f;
	return true;
}
