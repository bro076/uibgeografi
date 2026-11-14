#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DroneRaceCourse.h"
#include "DroneSimGameMode.generated.h"

class ADroneTerrain;
class ADronePawn;
class APlayerController;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class USkyLightComponent;

/**
 * Spawns the drone and HUD and, when the map has no lighting of its own (the default
 * empty map), builds the sky and one of the built-in landscapes around the origin.
 * Open a map of your own with a directional light in it and none of that happens.
 *
 * Also owns what all drones share: the weather, and one extra split-screen player
 * (with their own drone) for every additional phone that connects.
 */
UCLASS()
class DRONESIM_API ADroneSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADroneSimGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Switch to the next built-in landscape and put the drone back on its take-off point. */
	UFUNCTION(BlueprintCallable, Category = "Drone") void CycleScene();
	UFUNCTION(BlueprintCallable, Category = "Drone") void LoadScene(int32 Index);

	/** Name of the current landscape, empty when running in a custom map. */
	UFUNCTION(BlueprintPure, Category = "Drone") FString GetSceneName() const;

	/** Take-off point (cm) for a drone slot and the radius it must stay within (m). False in a custom map. */
	bool GetSceneSpawn(int32 Slot, FVector& OutLocation, float& OutFenceRadiusM) const;

	/** Steps through the fixed weather types, then "changing weather", then back to the first. */
	UFUNCTION(BlueprintCallable, Category = "Drone") void CycleWeather();

	/** e.g. "Breezy 5 m/s" or "Changing: Fog 2 m/s". */
	UFUNCTION(BlueprintPure, Category = "Drone") FString GetWeatherLabel() const;
	UFUNCTION(BlueprintPure, Category = "Drone") float GetTemperatureC() const { return TemperatureC; }
	UFUNCTION(BlueprintPure, Category = "Drone") bool IsRaining() const { return RainAmount > 0.5f; }

	// ---- Race: gates to fly through in order and small boxes to land on, timed per drone

	/** Builds the course, puts every drone back on its take-off point and starts the countdown. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Race") void StartRace();
	/** Removes the course and returns to free flight. */
	UFUNCTION(BlueprintCallable, Category = "Drone|Race") void EndRace();
	UFUNCTION(BlueprintPure, Category = "Drone|Race") bool IsRaceActive() const { return RacePhase != ERacePhase::Off; }

	/** Whole seconds left before the start (3, 2, 1), or 0 once the race is running. */
	int32 GetRaceCountdown() const;
	/** One line for a drone, e.g. "Gate 3/10  0:42.3" or "FINISHED 1:12.4 (2nd)". Empty when not racing. */
	FString GetRaceLine(int32 Slot) const;
	/** Where that drone has to go next. False when not racing or already finished. */
	bool GetRaceTarget(int32 Slot, FVector& OutLocation, bool& bOutLanding, int32& OutNumber) const;
	/** Standings, best first, one line per drone that is in the race. */
	TArray<FString> GetRaceStandings() const;

	/** Number of gates in a course (landing boxes are added on top: one halfway, one at the end). */
	UPROPERTY(EditAnywhere, Category = "Drone|Race", meta = (ClampMin = "2", ClampMax = "24")) int32 RaceGates = 8;
	/** How far from the take-off point the course loops, metres. */
	UPROPERTY(EditAnywhere, Category = "Drone|Race") float RaceRadiusMinM = 90.f;
	UPROPERTY(EditAnywhere, Category = "Drone|Race") float RaceRadiusMaxM = 190.f;
	/** Gate height above the ground below it, metres. Keep the minimum above the treetops. */
	UPROPERTY(EditAnywhere, Category = "Drone|Race") float RaceHeightMinM = 22.f;
	UPROPERTY(EditAnywhere, Category = "Drone|Race") float RaceHeightMaxM = 40.f;

	/** Seconds between weather changes in "changing weather" mode. */
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherChangeMinSeconds = 50.f;
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherChangeMaxSeconds = 110.f;
	/** How long one weather type takes to turn into the next. */
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherTransitionSeconds = 25.f;

private:
	void SpawnSky();
	void UpdateWeather(float DeltaSeconds);
	void UpdatePhonePlayers(float DeltaSeconds);
	void UpdateRace(float DeltaSeconds);
	void BuildRaceCourse();
	float GroundHeightCm(double X, double Y) const;
	ADronePawn* FindDrone(int32 Slot) const;
	static FString FormatRaceTime(float Seconds);

	UPROPERTY() TObjectPtr<ADroneTerrain> Terrain;
	int32 SceneIndex = -1;
	bool bBuiltInScenes = false;

	// ---- Sky pieces the weather drives (only set when this game mode spawned the sky)
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> SunLight;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;

	// ---- Weather: current values ease towards the target type
	int32 WeatherIndex = 1;
	bool bChangingWeather = false;
	float NextWeatherChange = 60.f;
	float WindSpeed = 3.f;
	float WindHeadingDeg = 60.f;
	float GustFraction = 0.5f;
	float FogDensity = 0.006f;
	float SunIntensity = 9.f;
	float RainAmount = 0.f;
	float TemperatureC = 12.f;
	float WeatherTime = 0.f;

	// ---- Race
	enum class ERacePhase : uint8 { Off, Countdown, Running };
	struct FRacer
	{
		int32 NextPoint = 0;
		float FinishTime = -1.f;   // < 0 while still racing
	};
	ERacePhase RacePhase = ERacePhase::Off;
	float RaceClock = 0.f;         // negative during the countdown, then seconds since the start
	TArray<FDroneRacePoint> RacePoints;
	static constexpr int32 MaxRacers = 4;
	FRacer Racers[MaxRacers];
	UPROPERTY() TObjectPtr<ADroneRaceCourse> RaceCourse;

	// ---- Extra split-screen players, one per phone slot above 0
	UPROPERTY() TMap<int32, TObjectPtr<APlayerController>> SlotControllers;
	TMap<int32, float> SlotIdleSeconds;
};
