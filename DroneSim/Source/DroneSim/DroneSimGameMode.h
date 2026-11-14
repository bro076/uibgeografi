#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DroneSimGameMode.generated.h"

class ADroneTerrain;
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

	/** Seconds between weather changes in "changing weather" mode. */
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherChangeMinSeconds = 50.f;
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherChangeMaxSeconds = 110.f;
	/** How long one weather type takes to turn into the next. */
	UPROPERTY(EditAnywhere, Category = "Drone|Weather") float WeatherTransitionSeconds = 25.f;

private:
	void SpawnSky();
	void UpdateWeather(float DeltaSeconds);
	void UpdatePhonePlayers(float DeltaSeconds);

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

	// ---- Extra split-screen players, one per phone slot above 0
	UPROPERTY() TMap<int32, TObjectPtr<APlayerController>> SlotControllers;
	TMap<int32, float> SlotIdleSeconds;
};
