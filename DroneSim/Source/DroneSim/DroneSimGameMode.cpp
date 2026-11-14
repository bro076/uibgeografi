#include "DroneSimGameMode.h"
#include "DronePawn.h"
#include "DroneHUD.h"

ADroneSimGameMode::ADroneSimGameMode()
{
	DefaultPawnClass = ADronePawn::StaticClass();
	HUDClass = ADroneHUD::StaticClass();
}
