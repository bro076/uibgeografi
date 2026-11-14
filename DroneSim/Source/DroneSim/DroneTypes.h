#pragma once

#include "CoreMinimal.h"
#include "DroneTypes.generated.h"

/** DJI-style flight modes (the C / N / S switch on the remote). */
UENUM(BlueprintType)
enum class EDroneFlightMode : uint8
{
	Cine,
	Normal,
	Sport
};

UENUM(BlueprintType)
enum class EDroneState : uint8
{
	MotorsOff,
	Idle,          // motors spinning, on the ground
	TakingOff,     // auto take-off to hover height
	Flying,
	Landing,       // auto landing
	ReturningHome,
	Crashed
};

/** Which stick does what. DJI ships with Mode 2. */
UENUM(BlueprintType)
enum class EDroneStickMode : uint8
{
	Mode1,   // left: pitch / yaw        right: throttle / roll
	Mode2,   // left: throttle / yaw     right: pitch / roll
	Mode3    // left: throttle / roll    right: pitch / yaw
};

UENUM(BlueprintType)
enum class EDroneCameraView : uint8
{
	FPV,     // gimbal camera, what the DJI Fly app shows
	Chase,   // third person behind the drone
	Pilot    // standing at the take-off point, visual line of sight
};

/** Speed / tilt envelope for one flight mode. */
USTRUCT(BlueprintType)
struct FDroneModeLimits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone") float MaxSpeed = 15.f;        // m/s horizontal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone") float MaxAscent = 6.f;        // m/s
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone") float MaxDescent = 6.f;       // m/s
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone") float MaxTiltDeg = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone") float MaxYawRateDeg = 120.f;  // deg/s
};

/** Raw stick deflection, -1..1, right / up positive. Not yet mapped through the stick mode. */
struct FDroneSticks
{
	float LeftX = 0.f;
	float LeftY = 0.f;
	float RightX = 0.f;
	float RightY = 0.f;
	float Gimbal = 0.f;   // gimbal dial rate, up positive

	void Clamp()
	{
		LeftX = FMath::Clamp(LeftX, -1.f, 1.f);
		LeftY = FMath::Clamp(LeftY, -1.f, 1.f);
		RightX = FMath::Clamp(RightX, -1.f, 1.f);
		RightY = FMath::Clamp(RightY, -1.f, 1.f);
		Gimbal = FMath::Clamp(Gimbal, -1.f, 1.f);
	}
};
