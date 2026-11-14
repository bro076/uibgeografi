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
#include "Math/RandomStream.h"
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
	const float RaceCountdownSeconds = 4.f;  // "get ready", then 3, 2, 1
	const float GateRadiusCm = 380.f;        // a drone this close to a gate centre has passed it
	const float MaxCourseRiseM = 95.f;       // gates stay below the 120 m ceiling above the take-off point
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

	if (AExponentialHeightFog* FogActor = World->SpawnActor<AExponentialHeightFog>())
	{
		if (UExponentialHeightFogComponent* Component = FogActor->GetComponent())
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

	EndRace();   // a course belongs to the landscape it was built on
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
	UpdateRace(DeltaSeconds);
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
				if (APlayerController* SlotPC = UGameplayStatics::CreatePlayer(this, -1, true))
				{
					SlotControllers.Add(Slot, SlotPC);
					if (ADronePawn* Drone = Cast<ADronePawn>(SlotPC->GetPawn()))
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
				if (APlayerController* SlotPC = Existing->Get())
				{
					UGameplayStatics::RemovePlayer(SlotPC, true);
				}
				SlotControllers.Remove(Slot);
				SlotIdleSeconds.Remove(Slot);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Race
// ---------------------------------------------------------------------------------------------

ADronePawn* ADroneSimGameMode::FindDrone(int32 Slot) const
{
	for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
	{
		if (It->GetPhoneSlot() == Slot)
		{
			return *It;
		}
	}
	return nullptr;
}

float ADroneSimGameMode::GroundHeightCm(double X, double Y) const
{
	if (Terrain)
	{
		// Built-in landscape: exact, and never below the fjord surface.
		return FMath::Max(0.f, Terrain->GetHeightM(X / 100.0, Y / 100.0)) * 100.f;
	}

	// Custom map: look down from high above.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DroneRaceGround), false);
	for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, 500000.0), FVector(X, Y, -500000.0), ECC_Visibility, Params))
	{
		return static_cast<float>(Hit.ImpactPoint.Z);
	}
	return 0.f;
}

void ADroneSimGameMode::BuildRaceCourse()
{
	RacePoints.Reset();

	// The course loops once around drone 1's take-off point, so it stays in sight of the pilots.
	FVector Home = FVector::ZeroVector;
	if (const ADronePawn* First = FindDrone(0))
	{
		Home = First->GetSpawnLocation();
	}
	const float MaxZ = static_cast<float>(Home.Z) + MaxCourseRiseM * 100.f;

	// Same course every time for a given landscape, so times can be compared between sessions.
	FRandomStream Random(9001 + SceneIndex * 131);
	const int32 NumGates = FMath::Clamp(RaceGates, 2, 24);
	const float StartAngle = Random.FRandRange(0.f, 360.f);

	auto PointAt = [&](float AngleDeg, float RadiusM)
	{
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		return FVector2D(Home.X + FMath::Cos(Rad) * RadiusM * 100.f, Home.Y + FMath::Sin(Rad) * RadiusM * 100.f);
	};

	// A landing box needs dry, not-too-high ground: try a few spots around the wanted one.
	auto AddLandingBox = [&](float AngleDeg, float RadiusM)
	{
		FVector2D Spot = PointAt(AngleDeg, RadiusM);
		for (int32 Try = 0; Try < 12; ++Try)
		{
			const float Ground = GroundHeightCm(Spot.X, Spot.Y);
			if (Ground > 150.f && Ground < MaxZ)
			{
				break;
			}
			Spot = PointAt(AngleDeg + 30.f * (Try + 1), RadiusM * (Try % 2 == 0 ? 0.7f : 1.f));
		}
		FDroneRacePoint Box;
		Box.bLanding = true;
		Box.YawDeg = Random.FRandRange(0.f, 90.f);
		// Sunk slightly so it sits firmly on sloping ground; Location is the top face.
		Box.Location = FVector(Spot.X, Spot.Y, GroundHeightCm(Spot.X, Spot.Y) + ADroneRaceCourse::LandingBoxM * 100.f - 20.f);
		RacePoints.Add(Box);
	};

	for (int32 i = 0; i < NumGates; ++i)
	{
		const float Angle = StartAngle + 360.f * i / NumGates;
		float RadiusM = Random.FRandRange(RaceRadiusMinM, FMath::Max(RaceRadiusMinM, RaceRadiusMaxM));
		const float HeightM = Random.FRandRange(RaceHeightMinM, FMath::Max(RaceHeightMinM, RaceHeightMaxM));

		// Pull the gate in towards the take-off point if the hillside there is too high.
		FVector2D Spot = PointAt(Angle, RadiusM);
		for (int32 Try = 0; Try < 6 && GroundHeightCm(Spot.X, Spot.Y) + HeightM * 100.f > MaxZ; ++Try)
		{
			RadiusM *= 0.7f;
			Spot = PointAt(Angle, RadiusM);
		}

		FDroneRacePoint Gate;
		Gate.Location = FVector(Spot.X, Spot.Y, FMath::Min(GroundHeightCm(Spot.X, Spot.Y) + HeightM * 100.f, MaxZ));
		RacePoints.Add(Gate);

		if (i == NumGates / 2 - 1)
		{
			// Halfway: come down, land on a box inside the loop, take off again.
			AddLandingBox(Angle + 180.f / NumGates, RaceRadiusMinM * 0.5f);
		}
	}

	// Finish: a box about 15 m in front of the take-off line.
	AddLandingBox(StartAngle, 15.f);

	// Each gate faces the way the drones arrive.
	FVector Previous = Home;
	for (FDroneRacePoint& Point : RacePoints)
	{
		if (!Point.bLanding)
		{
			Point.YawDeg = static_cast<float>((Point.Location - Previous).Rotation().Yaw);
		}
		Previous = Point.Location;
	}
}

void ADroneSimGameMode::StartRace()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	BuildRaceCourse();
	if (!RaceCourse)
	{
		RaceCourse = World->SpawnActor<ADroneRaceCourse>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	if (RaceCourse)
	{
		RaceCourse->Build(RacePoints);
	}

	for (FRacer& Racer : Racers)
	{
		Racer = FRacer();
	}
	for (TActorIterator<ADronePawn> It(World); It; ++It)
	{
		It->ResetDrone();
		It->SetInputLocked(true);   // nobody leaves before the start
	}
	RaceClock = -RaceCountdownSeconds;
	RacePhase = ERacePhase::Countdown;
}

void ADroneSimGameMode::EndRace()
{
	if (RaceCourse)
	{
		RaceCourse->Destroy();
		RaceCourse = nullptr;
	}
	RacePoints.Reset();
	if (RacePhase != ERacePhase::Off)
	{
		RacePhase = ERacePhase::Off;
		for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
		{
			It->SetInputLocked(false);
		}
	}
}

void ADroneSimGameMode::UpdateRace(float DeltaSeconds)
{
	if (RacePhase == ERacePhase::Off)
	{
		return;
	}

	RaceClock += DeltaSeconds;
	if (RacePhase == ERacePhase::Countdown)
	{
		// Drones that join during the countdown are held too.
		for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
		{
			It->SetInputLocked(RaceClock < 0.f);
		}
		if (RaceClock < 0.f)
		{
			return;
		}
		RacePhase = ERacePhase::Running;
	}

	for (TActorIterator<ADronePawn> It(GetWorld()); It; ++It)
	{
		const int32 Slot = It->GetPhoneSlot();
		if (Slot < 0 || Slot >= MaxRacers)
		{
			continue;
		}
		FRacer& Racer = Racers[Slot];
		if (Racer.FinishTime >= 0.f || !RacePoints.IsValidIndex(Racer.NextPoint))
		{
			continue;
		}

		const FDroneRacePoint& Point = RacePoints[Racer.NextPoint];
		const FVector Location = It->GetActorLocation();
		bool bReached = false;
		if (Point.bLanding)
		{
			// Touched down on top of the box (the drone's centre rests a little above the surface).
			const float Reach = ADroneRaceCourse::LandingBoxM * 50.f * 1.42f + 10.f;   // box corners count
			bReached = It->IsOnGround()
				&& FVector::Dist2D(Location, Point.Location) < Reach
				&& FMath::Abs(Location.Z - Point.Location.Z) < 70.0;
		}
		else
		{
			bReached = FVector::Dist(Location, Point.Location) < GateRadiusCm;
		}

		if (bReached)
		{
			++Racer.NextPoint;
			if (Racer.NextPoint >= RacePoints.Num())
			{
				Racer.FinishTime = RaceClock;
			}
		}
	}
}

int32 ADroneSimGameMode::GetRaceCountdown() const
{
	if (RacePhase != ERacePhase::Countdown)
	{
		return 0;
	}
	return FMath::Clamp(FMath::CeilToInt(-RaceClock), 1, 3);
}

FString ADroneSimGameMode::FormatRaceTime(float Seconds)
{
	const int32 Minutes = FMath::FloorToInt(Seconds / 60.f);
	return FString::Printf(TEXT("%d:%04.1f"), Minutes, Seconds - Minutes * 60.f);
}

bool ADroneSimGameMode::GetRaceTarget(int32 Slot, FVector& OutLocation, bool& bOutLanding, int32& OutNumber) const
{
	if (RacePhase == ERacePhase::Off || Slot < 0 || Slot >= MaxRacers || !RacePoints.IsValidIndex(Racers[Slot].NextPoint))
	{
		return false;
	}
	const FDroneRacePoint& Point = RacePoints[Racers[Slot].NextPoint];
	OutLocation = Point.Location;
	bOutLanding = Point.bLanding;
	OutNumber = Racers[Slot].NextPoint + 1;
	return true;
}

FString ADroneSimGameMode::GetRaceLine(int32 Slot) const
{
	if (RacePhase == ERacePhase::Off || Slot < 0 || Slot >= MaxRacers)
	{
		return FString();
	}
	if (RacePhase == ERacePhase::Countdown)
	{
		return FString::Printf(TEXT("RACE STARTS IN %d"), GetRaceCountdown());
	}

	const FRacer& Racer = Racers[Slot];
	if (Racer.FinishTime >= 0.f)
	{
		int32 Place = 1;
		for (const FRacer& Other : Racers)
		{
			if (Other.FinishTime >= 0.f && Other.FinishTime < Racer.FinishTime)
			{
				++Place;
			}
		}
		static const TCHAR* Places[] = { TEXT("1st"), TEXT("2nd"), TEXT("3rd"), TEXT("4th") };
		return FString::Printf(TEXT("FINISHED %s (%s)"), *FormatRaceTime(Racer.FinishTime), Places[FMath::Clamp(Place, 1, 4) - 1]);
	}

	const bool bLanding = RacePoints.IsValidIndex(Racer.NextPoint) && RacePoints[Racer.NextPoint].bLanding;
	return FString::Printf(TEXT("%s %d/%d  %s"), bLanding ? TEXT("LAND ON BOX") : TEXT("GATE"), Racer.NextPoint + 1, RacePoints.Num(), *FormatRaceTime(RaceClock));
}

TArray<FString> ADroneSimGameMode::GetRaceStandings() const
{
	TArray<FString> Lines;
	if (RacePhase != ERacePhase::Running)
	{
		return Lines;
	}

	// Finished drones by time, then the rest by how far they have got.
	TArray<int32> Order;
	for (int32 Slot = 0; Slot < MaxRacers; ++Slot)
	{
		if (FindDrone(Slot))
		{
			Order.Add(Slot);
		}
	}
	Order.Sort([this](int32 A, int32 B)
	{
		const FRacer& RA = Racers[A];
		const FRacer& RB = Racers[B];
		const bool bDoneA = RA.FinishTime >= 0.f;
		const bool bDoneB = RB.FinishTime >= 0.f;
		if (bDoneA != bDoneB) { return bDoneA; }
		if (bDoneA) { return RA.FinishTime < RB.FinishTime; }
		return RA.NextPoint > RB.NextPoint;
	});

	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FRacer& Racer = Racers[Order[i]];
		Lines.Add(Racer.FinishTime >= 0.f
			? FString::Printf(TEXT("%d.  Drone %d   %s"), i + 1, Order[i] + 1, *FormatRaceTime(Racer.FinishTime))
			: FString::Printf(TEXT("%d.  Drone %d   %d/%d"), i + 1, Order[i] + 1, Racer.NextPoint, RacePoints.Num()));
	}
	return Lines;
}
