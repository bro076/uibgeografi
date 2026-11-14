#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DroneSimGameMode.generated.h"

class ADroneTerrain;

/**
 * Spawns the drone and HUD and, when the map has no lighting of its own (the default
 * empty map), builds the sky and one of the built-in landscapes around the origin.
 * Open a map of your own with a directional light in it and none of that happens.
 */
UCLASS()
class DRONESIM_API ADroneSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADroneSimGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** Switch to the next built-in landscape and put the drone back on its take-off point. */
	UFUNCTION(BlueprintCallable, Category = "Drone") void CycleScene();
	UFUNCTION(BlueprintCallable, Category = "Drone") void LoadScene(int32 Index);

	/** Name of the current landscape, empty when running in a custom map. */
	UFUNCTION(BlueprintPure, Category = "Drone") FString GetSceneName() const;

	/** Take-off point (cm) and the radius the drone must stay within (m). False in a custom map. */
	bool GetSceneSpawn(FVector& OutLocation, float& OutFenceRadiusM) const;

private:
	void SpawnSky();

	UPROPERTY() TObjectPtr<ADroneTerrain> Terrain;
	int32 SceneIndex = -1;
	bool bBuiltInScenes = false;
};
