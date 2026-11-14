#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DroneTypes.h"
#include "DronePawn.generated.h"

class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A DJI-style camera drone.
 *
 * The flight model is integrated by hand (not Chaos physics) so it behaves the same on
 * every machine: sticks command velocity in GPS modes, the flight controller tilts the
 * aircraft to get there, and thrust / gravity / drag / wind decide what actually happens.
 * In ATTI mode the sticks command tilt directly and nothing holds position.
 *
 * All simulation state is in metres and seconds; Unreal units (cm) are only used at the edges.
 */
UCLASS()
class DRONESIM_API ADronePawn : public APawn
{
	GENERATED_BODY()

public:
	ADronePawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	// ---- Commands (keyboard, gamepad, phone and Blueprint all end up here)
	UFUNCTION(BlueprintCallable, Category = "Drone") void ToggleTakeoffLand();
	UFUNCTION(BlueprintCallable, Category = "Drone") void ToggleReturnToHome();
	UFUNCTION(BlueprintCallable, Category = "Drone") void SetFlightMode(EDroneFlightMode NewMode);
	UFUNCTION(BlueprintCallable, Category = "Drone") void ToggleAtti();
	UFUNCTION(BlueprintCallable, Category = "Drone") void SetStickMode(EDroneStickMode NewMode);
	UFUNCTION(BlueprintCallable, Category = "Drone") void CycleCameraView();
	UFUNCTION(BlueprintCallable, Category = "Drone") void CycleWind();
	UFUNCTION(BlueprintCallable, Category = "Drone") void ResetDrone();

	// ---- Telemetry
	UFUNCTION(BlueprintPure, Category = "Drone") EDroneState GetDroneState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Drone") EDroneFlightMode GetFlightMode() const { return FlightMode; }
	UFUNCTION(BlueprintPure, Category = "Drone") EDroneStickMode GetStickMode() const { return StickMode; }
	UFUNCTION(BlueprintPure, Category = "Drone") EDroneCameraView GetCameraView() const { return CameraView; }
	UFUNCTION(BlueprintPure, Category = "Drone") bool IsAtti() const { return bAtti; }
	UFUNCTION(BlueprintPure, Category = "Drone") float GetHeightM() const;             // above the home point
	UFUNCTION(BlueprintPure, Category = "Drone") float GetDistanceM() const;           // horizontal, from the home point
	UFUNCTION(BlueprintPure, Category = "Drone") float GetHorizontalSpeed() const;     // m/s
	UFUNCTION(BlueprintPure, Category = "Drone") float GetVerticalSpeed() const;       // m/s
	UFUNCTION(BlueprintPure, Category = "Drone") float GetBatteryPercent() const { return BatteryPercent; }
	UFUNCTION(BlueprintPure, Category = "Drone") float GetWindSpeed() const { return WindSpeed; }
	UFUNCTION(BlueprintPure, Category = "Drone") float GetGimbalPitch() const { return GimbalPitch; }
	UFUNCTION(BlueprintPure, Category = "Drone") FString GetWarningText() const { return WarningText; }
	const FDroneSticks& GetRawSticks() const { return RawSticks; }

	// ---- Tuning (defaults approximate a Mavic 3 class aircraft)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") FDroneModeLimits CineLimits;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") FDroneModeLimits NormalLimits;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") FDroneModeLimits SportLimits;

	/** Stick expo: 0 = linear, 1 = fully cubic. Softens the response around centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight", meta = (ClampMin = "0", ClampMax = "1")) float StickExpo = 0.3f;
	/** How fast the aircraft reaches a commanded tilt (seconds, first-order lag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") float AttitudeTimeConstant = 0.12f;
	/** Velocity loop gain in GPS modes (1/s). Higher = snappier acceleration and braking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") float VelocityGain = 1.5f;
	/** Aerodynamic drag: accel = -(Linear + Quadratic * |v|) * v, v relative to the air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") float LinearDrag = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") float QuadraticDrag = 0.0108f;
	/** Maximum thrust as a multiple of weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Flight") float MaxThrustToWeight = 2.2f;

	/** Legal / firmware ceiling above the take-off point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Limits") float MaxHeightM = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Limits") float RthAltitudeM = 30.f;
	/** Distance at which the "beyond visual line of sight" warning appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Limits") float VlosWarningDistanceM = 500.f;
	/** Impact speed (m/s) that counts as a crash rather than a touch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Limits") float CrashSpeedGround = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Limits") float CrashSpeedObstacle = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Battery") float HoverFlightTimeMinutes = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Battery") float LowBatteryPercent = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Battery") float RthBatteryPercent = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Battery") float CriticalBatteryPercent = 8.f;

	/** Mean wind speed in m/s. Press G in game to step through calm / light / moderate / strong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Wind") float WindSpeed = 3.f;
	/** Compass-style direction the wind blows towards, degrees of world yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Wind") float WindHeadingDeg = 60.f;
	/** Gust size as a fraction of the mean wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Wind") float GustFraction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera") float CameraFov = 73.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera") float GimbalRateDeg = 45.f;
	/** In pilot view, zoom in gently as the drone gets far away so it stays visible on a monitor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Camera") bool bPilotViewZoomAssist = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input") float GamepadDeadzone = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Input") bool bInvertGamepadRightY = false;

	/** Turn off to hide the built-in box drone when you add your own mesh in a Blueprint subclass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone|Visual") bool bShowPlaceholderMesh = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Drone") TObjectPtr<USphereComponent> Collision;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Props;

private:
	struct FAxes
	{
		double Throttle = 0.0;
		double Yaw = 0.0;
		double Pitch = 0.0;
		double Roll = 0.0;
	};

	UStaticMeshComponent* AddPart(const FString& Name, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color);
	void TryPlaceOnGround(float DeltaSeconds);
	void GatherInput(float DeltaSeconds);
	void HandleCommand(const FString& Command);
	void UpdateStickGestures(float DeltaSeconds);
	void Simulate(double Dt);
	void MoveAndCollide(double Dt);
	void Crash(const FVector& Normal, double ImpactSpeed);
	void StartMotors();
	void StopMotors();
	void UpdateBatteryAndWarnings(float DeltaSeconds);
	void UpdateVisuals(float DeltaSeconds);
	void SendPhoneTelemetry(float DeltaSeconds);
	const FDroneModeLimits& GetLimits() const;
	FVector GetWindVelocity() const;
	bool AreMotorsRunning() const { return State != EDroneState::MotorsOff && State != EDroneState::Crashed; }

	TArray<FLinearColor> PartColors;

	// ---- Mode / state
	EDroneState State = EDroneState::MotorsOff;
	EDroneFlightMode FlightMode = EDroneFlightMode::Normal;
	EDroneStickMode StickMode = EDroneStickMode::Mode2;
	EDroneCameraView CameraView = EDroneCameraView::FPV;
	bool bAtti = false;

	// ---- Input
	FDroneSticks RawSticks;        // combined keyboard + gamepad + phone
	FDroneSticks KeyboardSticks;   // keyboard is ramped so taps are not full deflection
	FAxes Axes;                    // after stick mode mapping and expo
	double CscHoldTime = 0.0;
	bool bCscLatched = false;
	double ThrottleDownGroundTime = 0.0;

	// ---- Simulation state (metres, seconds, degrees)
	FVector Velocity = FVector::ZeroVector;
	FVector2D TiltTan = FVector2D::ZeroVector;   // world-frame lean, tan(angle); thrust points along (TiltTan, 1)
	double YawDeg = 0.0;
	double YawRateDeg = 0.0;
	double ThrustAccel = 0.0;                    // m/s^2 along the rotor axis
	double GroundDistanceM = 0.0;                // below the landing gear
	double RthTargetHeightM = 0.0;
	double SimTime = 0.0;
	bool bOnGround = true;
	bool bAtMaxHeight = false;
	bool bBatteryRthDone = false;

	// ---- Misc
	bool bPlaced = false;
	float PlaceTimer = 0.f;
	FVector SpawnLocation = FVector::ZeroVector;
	double SpawnYawDeg = 0.0;
	FVector HomeLocation = FVector::ZeroVector;   // cm, recorded when the motors start
	FVector PilotEyeLocation = FVector::ZeroVector;
	FRotator CrashSpin = FRotator::ZeroRotator;
	float BatteryPercent = 100.f;
	float GimbalPitch = 0.f;
	float PropAngle = 0.f;
	float TelemetryTimer = 0.f;
	int32 WindPreset = 1;
	FString WarningText;
};
