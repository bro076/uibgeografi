#include "DroneSimGameMode.h"

#include "DroneHUD.h"
#include "DronePawn.h"
#include "DroneTerrain.h"
#include "PhoneControllerSubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
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

	struct FDroneWeather
	{
		const TCHAR* Name;
		float WindSpeed;       // mean, m/s
		float GustFraction;    // gust size relative to the mean
		float FogDensity;      // exponential height fog density; 0.09 is roughly 100 m visibility
		float SunIntensity;    // lux scale of the directional light
		float Rain;            // 0 or 1
		float TemperatureC;    // at sea level
	};

	const FDroneWeather WeatherTypes[] =
	{
		{ TEXT("Calm"), 1.f, 0.3f, 0.004f, 9.5f, 0.f, 14.f },
		{ TEXT("Breezy"), 5.f, 0.5f, 0.006f, 9.f, 0.f, 12.f },
		{ TEXT("Overcast, windy"), 9.f, 0.6f, 0.014f, 3.5f, 0.f, 9.f },
		{ TEXT("Fog"), 2.f, 0.3f, 0.09f, 2.5f, 0.f, 6.f },
		{ TEXT("Rain squall"), 13.f, 0.8f, 0.03f, 1.8f, 1.f, 5.f },
	};
	const int32 NumWeatherTypes = UE_ARRAY_COUNT(WeatherTypes);

	const float DroneSpacingM = 2.5f;        // take-off points are lined up eastward
	const float PhoneGoneSeconds = 15.f;     // a drone is removed this long after its phone disconnects
}

ADroneSimGameMode::ADroneSimGameMode()
{
	DefaultPawnClass = ADronePawn::StaticClass();
	HUDClass = ADroneHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
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
			SunLight = Light;
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
			SkyLight = Light;
		}
	}

	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>())
	{
		if (UExponentialHeightFogComponent* Component = Fog->GetComponent())
		{
			Component->SetFogDensity(0.006f);
			Component->SetVolumetricFog(true);   // soft light shafts and haze in the valleys
			Fog = Component;
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
	for (TActorIterator<ADronePawn> It(World); It; ++It)
	{
		FVector Spawn;
		float FenceRadiusM = 0.f;
		if (GetSceneSpawn(It->GetPhoneSlot(), Spawn, FenceRadiusM))
		{
			It->SetSpawnPoint(Spawn, FenceRadiusM);
		}
	}
}

FString ADroneSimGameMode::GetSceneName() const
{
	return (bBuiltInScenes && SceneIndex >= 0) ? FString(Scenes[SceneIndex].Name) : FString();
}

bool ADroneSimGameMode::GetSceneSpawn(int32 Slot, FVector& OutLocation, float& OutFenceRadiusM) const
{
	if (!Terrain)
	{
		return false;
	}
	// The terrain file is centred on the take-off point; further drones line up to the east of it.
	const double EastM = FMath::Max(Slot, 0) * DroneSpacingM;
	OutLocation = FVector(0.0, EastM * 100.0, Terrain->GetHeightM(0.0, EastM) * 100.0);
	OutFenceRadiusM = Terrain->GetHalfExtentM() - 150.f;
	return true;
}

void ADroneSimGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateWeather(DeltaSeconds);
	UpdatePhonePlayers(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------
// Weather
// ---------------------------------------------------------------------------------------------

void ADroneSimGameMode::CycleWeather()
{
	// Calm -> Breezy -> Overcast -> Fog -> Rain squall -> Changing weather -> Calm ...
	if (bChangingWeather)
	{
		bChangingWeather = false;
		WeatherIndex = 0;
	}
	else if (WeatherIndex + 1 < NumWeatherTypes)
	{
		++WeatherIndex;
	}
	else
	{
		bChangingWeather = true;
		NextWeatherChange = WeatherTime + 20.f;   // first change comes quickly so the mode is noticeable
	}
}

FString ADroneSimGameMode::GetWeatherLabel() const
{
	return FString::Printf(TEXT("%s%s %.0f m/s"), bChangingWeather ? TEXT("Changing: ") : TEXT(""), WeatherTypes[WeatherIndex].Name, WindSpeed);
}

void ADroneSimGameMode::UpdateWeather(float DeltaSeconds)
{
	WeatherTime += DeltaSeconds;

	if (bChangingWeather && WeatherTime >= NextWeatherChange)
	{
		// Move to a different type; weather usually shifts to something nearby, sometimes jumps.
		int32 Next = WeatherIndex + (FMath::RandBool() ? 1 : -1);
		if (FMath::FRand() < 0.25f || Next < 0 || Next >= NumWeatherTypes)
		{
			Next = FMath::RandRange(0, NumWeatherTypes - 1);
		}
		if (Next == WeatherIndex)
		{
			Next = (WeatherIndex + 1) % NumWeatherTypes;
		}
		WeatherIndex = Next;
		NextWeatherChange = WeatherTime + FMath::FRandRange(WeatherChangeMinSeconds, FMath::Max(WeatherChangeMinSeconds, WeatherChangeMaxSeconds));
	}

	// Ease every value towards the current type.
	const FDroneWeather& Target = WeatherTypes[WeatherIndex];
	const float Blend = 1.f - FMath::Exp(-DeltaSeconds * 3.f / FMath::Max(WeatherTransitionSeconds, 0.1f));
	WindSpeed = FMath::Lerp(WindSpeed, Target.WindSpeed, Blend);
	GustFraction = FMath::Lerp(GustFraction, Target.GustFraction, Blend);
	FogDensity = FMath::Lerp(FogDensity, Target.FogDensity, Blend);
	SunIntensity = FMath::Lerp(SunIntensity, Target.SunIntensity, Blend);
	RainAmount = FMath::Lerp(RainAmount, Target.Rain, Blend);

	// The wind direction wanders slowly, faster in unsettled weather.
	WindHeadingDeg += FMath::PerlinNoise1D(WeatherTime * 0.02f) * (bChangingWeather ? 4.f : 1.5f) * DeltaSeconds;

	// About 6.5 degrees colder per 1000 m: the glacier take-off point is well below freezing in bad weather.
	float GroundHeightM = 0.f;
	if (Terrain)
	{
		GroundHeightM = Terrain->GetHeightM(0.0, 0.0);
	}
	TemperatureC = FMath::Lerp(TemperatureC, Target.TemperatureC - 0.0065f * GroundHeightM, Blend);

	// Sky, when this game mode built it.
	if (SunLight)
	{
		SunLight->SetIntensity(SunIntensity);
	}
	if (SkyLight)
	{
		SkyLight->SetIntensity(FMath::GetMappedRangeValueClamped(FVector2f(1.8f, 9.5f), FVector2f(0.6f, 1.f), SunIntensity));
	}
	if (Fog)
	{
		Fog->SetFogDensity(FogDensity);
	}

	for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
	{
		It->SetWeather(WindSpeed, WindHeadingDeg, GustFraction, TemperatureC, RainAmount > 0.5f);
	}
}

// ---------------------------------------------------------------------------------------------
// One drone per phone
// ---------------------------------------------------------------------------------------------

void ADroneSimGameMode::UpdatePhonePlayers(float DeltaSeconds)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPhoneControllerSubsystem* Phone = GameInstance ? GameInstance->GetSubsystem<UPhoneControllerSubsystem>() : nullptr;
	if (!Phone)
	{
		return;
	}

	// Slot 0 is the player that always exists (keyboard, gamepad and the first phone).
	for (int32 Slot = 1; Slot < UPhoneControllerSubsystem::MaxSlots; ++Slot)
	{
		const bool bPhoneConnected = Phone->IsSlotActive(Slot);
		TObjectPtr<APlayerController>* Existing = SlotControllers.Find(Slot);

		if (bPhoneConnected)
		{
			SlotIdleSeconds.Remove(Slot);
			if (!Existing)
			{
				// A new local player gets its own controller, drone, HUD and a share of the screen.
				if (APlayerController* Controller = UGameplayStatics::CreatePlayer(this, -1, true))
				{
					SlotControllers.Add(Slot, Controller);
					if (ADronePawn* Drone = Cast<ADronePawn>(Controller->GetPawn()))
					{
						Drone->SetPhoneSlot(Slot);
					}
				}
			}
		}
		else if (Existing)
		{
			// Keep the drone for a while so a dropped connection or a page reload does not lose it.
			float& Idle = SlotIdleSeconds.FindOrAdd(Slot);
			Idle += DeltaSeconds;
			if (Idle > PhoneGoneSeconds)
			{
				if (APlayerController* Controller = Existing->Get())
				{
					UGameplayStatics::RemovePlayer(Controller, true);
				}
				SlotControllers.Remove(Slot);
				SlotIdleSeconds.Remove(Slot);
			}
		}
	}
}
