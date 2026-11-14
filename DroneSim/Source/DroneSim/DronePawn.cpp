#include "DronePawn.h"

#include "DroneSimGameMode.h"
#include "PhoneControllerSubsystem.h"
#include "Camera/CameraTypes.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const double Gravity = 9.81;
	const float CollisionRadiusCm = 18.f;
	const double HoverHeightM = 1.2;        // auto take-off height
	const double CscHoldSeconds = 0.7;      // stick gesture hold time to start / stop motors
	const double LandHoldSeconds = 0.8;     // throttle-down hold on the ground to stop motors
	const float GimbalMinPitch = -90.f;
	const float GimbalMaxPitch = 30.f;
	const float WindPresets[] = { 0.f, 3.f, 7.f, 11.f };

	double ApplyExpo(double Value, double Expo)
	{
		return (1.0 - Expo) * Value + Expo * Value * Value * Value;
	}

	float ApplyDeadzone(float Value, float Deadzone)
	{
		const float Magnitude = FMath::Abs(Value);
		if (Magnitude <= Deadzone)
		{
			return 0.f;
		}
		return FMath::Sign(Value) * (Magnitude - Deadzone) / (1.f - Deadzone);
	}
}

ADronePawn::ADronePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CineLimits.MaxSpeed = 5.f;
	CineLimits.MaxAscent = 1.f;
	CineLimits.MaxDescent = 1.f;
	CineLimits.MaxTiltDeg = 25.f;
	CineLimits.MaxYawRateDeg = 60.f;

	NormalLimits.MaxSpeed = 15.f;
	NormalLimits.MaxAscent = 6.f;
	NormalLimits.MaxDescent = 6.f;
	NormalLimits.MaxTiltDeg = 30.f;
	NormalLimits.MaxYawRateDeg = 120.f;

	SportLimits.MaxSpeed = 21.f;
	SportLimits.MaxAscent = 8.f;
	SportLimits.MaxDescent = 6.f;
	SportLimits.MaxTiltDeg = 35.f;
	SportLimits.MaxYawRateDeg = 200.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(CollisionRadiusCm);
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = Collision;

	// Placeholder aircraft built from the engine's basic cube (100 cm), so the project
	// needs no art assets. Swap it for a proper mesh in a Blueprint subclass.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cube = CubeFinder.Object;

	const FLinearColor BodyColor(0.18f, 0.19f, 0.2f);
	const FLinearColor DarkColor(0.03f, 0.03f, 0.03f);

	AddPart(TEXT("Body"), Cube, FVector(0, 0, 0), FRotator::ZeroRotator, FVector(0.24f, 0.09f, 0.06f), BodyColor);
	AddPart(TEXT("Gimbal"), Cube, FVector(13, 0, -4), FRotator::ZeroRotator, FVector(0.04f, 0.05f, 0.04f), DarkColor);

	for (int32 i = 0; i < 4; ++i)
	{
		const float SignX = (i < 2) ? 1.f : -1.f;            // front pair first
		const float SignY = (i % 2 == 0) ? 1.f : -1.f;
		const FVector MotorLocation(SignX * 11.f, SignY * 13.f, 2.f);
		const float ArmYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(MotorLocation.Y, MotorLocation.X)));

		AddPart(FString::Printf(TEXT("Arm%d"), i), Cube, MotorLocation * 0.5f, FRotator(0.f, ArmYaw, 0.f), FVector(0.17f, 0.02f, 0.015f), BodyColor);
		AddPart(FString::Printf(TEXT("Motor%d"), i), Cube, MotorLocation, FRotator::ZeroRotator, FVector(0.03f, 0.03f, 0.03f), DarkColor);
		// Front arm lights red, rear green, like the real aircraft - the key orientation cue in line-of-sight flying.
		AddPart(FString::Printf(TEXT("Light%d"), i), Cube, MotorLocation + FVector(0, 0, -2.5f), FRotator::ZeroRotator, FVector(0.02f, 0.02f, 0.012f),
			SignX > 0 ? FLinearColor(1.f, 0.02f, 0.02f) : FLinearColor(0.02f, 1.f, 0.05f));

		UStaticMeshComponent* Prop = AddPart(FString::Printf(TEXT("Prop%d"), i), Cube, MotorLocation + FVector(0, 0, 2.5f), FRotator::ZeroRotator, FVector(0.2f, 0.018f, 0.004f), DarkColor);
		Props.Add(Prop);
	}
}

UStaticMeshComponent* ADronePawn::AddPart(const FString& Name, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(*Name));
	Part->SetupAttachment(RootComponent);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeRotation(Rotation);
	Part->SetRelativeScale3D(Scale);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Parts.Add(Part);
	PartColors.Add(Color);
	return Part;
}

void ADronePawn::BeginPlay()
{
	Super::BeginPlay();

	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		if (!Parts[i])
		{
			continue;
		}
		Parts[i]->SetVisibility(bShowPlaceholderMesh);
		if (UMaterialInstanceDynamic* Material = Parts[i]->CreateDynamicMaterialInstance(0))
		{
			// "Color" is the parameter exposed by the engine's BasicShapeMaterial.
			Material->SetVectorParameterValue(TEXT("Color"), PartColors.IsValidIndex(i) ? PartColors[i] : FLinearColor::Gray);
		}
	}

	YawDeg = GetActorRotation().Yaw;
	SpawnYawDeg = YawDeg;
	SpawnLocation = GetActorLocation();
	HomeLocation = SpawnLocation;
	WindPreset = 1;

	// Built-in landscapes know exactly where the take-off point is; otherwise Tick finds the ground by tracing.
	if (const ADroneSimGameMode* GameMode = GetWorld()->GetAuthGameMode<ADroneSimGameMode>())
	{
		FVector Spawn;
		float Fence = 0.f;
		if (GameMode->GetSceneSpawn(PhoneSlot, Spawn, Fence))
		{
			SetSpawnPoint(Spawn, Fence);
		}
	}
}

void ADronePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	DeltaSeconds = FMath::Min(DeltaSeconds, 0.05f);   // do not let a hitch explode the integration
	SimTime += DeltaSeconds;

	GatherInput(DeltaSeconds);

	if (!bPlaced)
	{
		TryPlaceOnGround(DeltaSeconds);
		return;
	}

	// Height above whatever is directly below (used for landing and landing protection).
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DroneGroundTrace), false, this);
		const FVector Start = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 50000.0), ECC_Visibility, Params))
		{
			GroundDistanceM = FMath::Max<double>(0.0, (Hit.Distance - CollisionRadiusCm) / 100.0);
		}
		else
		{
			GroundDistanceM = 500.0;
		}
	}

	UpdateStickGestures(DeltaSeconds);

	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / 0.01f), 1, 6);
	const double StepDt = static_cast<double>(DeltaSeconds) / Steps;
	for (int32 i = 0; i < Steps; ++i)
	{
		Simulate(StepDt);
	}

	GimbalPitch = FMath::Clamp(GimbalPitch + RawSticks.Gimbal * GimbalRateDeg * DeltaSeconds, GimbalMinPitch, GimbalMaxPitch);

	UpdateBatteryAndWarnings(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);
	SendPhoneTelemetry(DeltaSeconds);
}

void ADronePawn::TryPlaceOnGround(float DeltaSeconds)
{
	// Sit the drone on the surface below the spawn point. Retried for a few seconds because
	// streamed (World Partition) landscapes may not have collision on the first frames.
	const FVector Location = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DronePlaceTrace), false, this);
	FHitResult Hit;

	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 200.0), Location - FVector(0, 0, 100000.0), ECC_Visibility, Params);
	if (!bHit)
	{
		// Maybe we spawned below the terrain: look from far above instead.
		bHit = GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 200000.0), Location, ECC_Visibility, Params);
	}

	PlaceTimer += DeltaSeconds;
	if (!bHit && PlaceTimer < 3.f)
	{
		return;
	}

	if (bHit)
	{
		SetActorLocation(Hit.ImpactPoint + FVector(0, 0, CollisionRadiusCm + 1.f), false);
		bOnGround = true;
	}
	else
	{
		bOnGround = false;   // nothing below us; it will simply fall
	}

	bPlaced = true;
	SpawnLocation = GetActorLocation();
	HomeLocation = SpawnLocation;
	PilotEyeLocation = SpawnLocation + FRotator(0.0, SpawnYawDeg, 0.0).RotateVector(FVector(-300.0, 0.0, 150.0));
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void ADronePawn::GatherInput(float DeltaSeconds)
{
	FDroneSticks Sticks;

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Keyboard: W/S A/D = left stick, arrow keys = right stick. Ramped so it is flyable.
		auto KeyAxis = [PC](const FKey& Positive, const FKey& Negative)
		{
			return (PC->IsInputKeyDown(Positive) ? 1.f : 0.f) - (PC->IsInputKeyDown(Negative) ? 1.f : 0.f);
		};
		const float RampSpeed = 5.f;
		KeyboardSticks.LeftX = FMath::FInterpConstantTo(KeyboardSticks.LeftX, KeyAxis(EKeys::D, EKeys::A), DeltaSeconds, RampSpeed);
		KeyboardSticks.LeftY = FMath::FInterpConstantTo(KeyboardSticks.LeftY, KeyAxis(EKeys::W, EKeys::S), DeltaSeconds, RampSpeed);
		KeyboardSticks.RightX = FMath::FInterpConstantTo(KeyboardSticks.RightX, KeyAxis(EKeys::Right, EKeys::Left), DeltaSeconds, RampSpeed);
		KeyboardSticks.RightY = FMath::FInterpConstantTo(KeyboardSticks.RightY, KeyAxis(EKeys::Up, EKeys::Down), DeltaSeconds, RampSpeed);
		Sticks = KeyboardSticks;
		Sticks.Gimbal = KeyAxis(EKeys::PageUp, EKeys::PageDown);

		// Gamepad / USB RC transmitter
		Sticks.LeftX += ApplyDeadzone(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), GamepadDeadzone);
		Sticks.LeftY += ApplyDeadzone(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), GamepadDeadzone);
		Sticks.RightX += ApplyDeadzone(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), GamepadDeadzone);
		Sticks.RightY += ApplyDeadzone(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY), GamepadDeadzone) * (bInvertGamepadRightY ? -1.f : 1.f);
		Sticks.Gimbal += PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) - PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);

		if (PC->WasInputKeyJustPressed(EKeys::T) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) { ToggleTakeoffLand(); }
		if (PC->WasInputKeyJustPressed(EKeys::H) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)) { ToggleReturnToHome(); }
		if (PC->WasInputKeyJustPressed(EKeys::C) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top)) { CycleCameraView(); }
		if (PC->WasInputKeyJustPressed(EKeys::One)) { SetFlightMode(EDroneFlightMode::Cine); }
		if (PC->WasInputKeyJustPressed(EKeys::Two)) { SetFlightMode(EDroneFlightMode::Normal); }
		if (PC->WasInputKeyJustPressed(EKeys::Three)) { SetFlightMode(EDroneFlightMode::Sport); }
		if (PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left))
		{
			SetFlightMode(static_cast<EDroneFlightMode>((static_cast<int32>(FlightMode) + 1) % 3));
		}
		if (PC->WasInputKeyJustPressed(EKeys::Four)) { ToggleAtti(); }
		if (PC->WasInputKeyJustPressed(EKeys::M))
		{
			SetStickMode(static_cast<EDroneStickMode>((static_cast<int32>(StickMode) + 1) % 3));
		}
		if (PC->WasInputKeyJustPressed(EKeys::G)) { CycleWind(); }
		if (PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)) { PauseFlight(); }
		if (PC->WasInputKeyJustPressed(EKeys::R)) { ResetDrone(); }
		if (PC->WasInputKeyJustPressed(EKeys::F2)) { HandleCommand(TEXT("scene")); }
	}

	// Phone controller
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPhoneControllerSubsystem* Phone = GameInstance->GetSubsystem<UPhoneControllerSubsystem>())
		{
			FDroneSticks PhoneSticks;
			if (Phone->GetSticks(PhoneSlot, PhoneSticks))
			{
				Sticks.LeftX += PhoneSticks.LeftX;
				Sticks.LeftY += PhoneSticks.LeftY;
				Sticks.RightX += PhoneSticks.RightX;
				Sticks.RightY += PhoneSticks.RightY;
				Sticks.Gimbal += PhoneSticks.Gimbal;
			}

			TArray<FString> Commands;
			Phone->ConsumeCommands(PhoneSlot, Commands);
			for (const FString& Command : Commands)
			{
				HandleCommand(Command);
			}
		}
	}

	Sticks.Clamp();
	RawSticks = Sticks;

	// Map the physical sticks to flight axes.
	double Throttle = 0.0, Yaw = 0.0, Pitch = 0.0, Roll = 0.0;
	switch (StickMode)
	{
	case EDroneStickMode::Mode1:
		Pitch = Sticks.LeftY; Yaw = Sticks.LeftX; Throttle = Sticks.RightY; Roll = Sticks.RightX;
		break;
	case EDroneStickMode::Mode3:
		Throttle = Sticks.LeftY; Roll = Sticks.LeftX; Pitch = Sticks.RightY; Yaw = Sticks.RightX;
		break;
	case EDroneStickMode::Mode2:
	default:
		Throttle = Sticks.LeftY; Yaw = Sticks.LeftX; Pitch = Sticks.RightY; Roll = Sticks.RightX;
		break;
	}
	Axes.Throttle = ApplyExpo(Throttle, StickExpo);
	Axes.Yaw = ApplyExpo(Yaw, StickExpo);
	Axes.Pitch = ApplyExpo(Pitch, StickExpo);
	Axes.Roll = ApplyExpo(Roll, StickExpo);
}

void ADronePawn::HandleCommand(const FString& Command)
{
	FString Name = Command;
	FString Argument;
	Command.Split(TEXT("|"), &Name, &Argument);
	const int32 Value = FCString::Atoi(*Argument);

	if (Name == TEXT("takeoff")) { ToggleTakeoffLand(); }
	else if (Name == TEXT("rth")) { ToggleReturnToHome(); }
	else if (Name == TEXT("mode")) { SetFlightMode(static_cast<EDroneFlightMode>(FMath::Clamp(Value, 0, 2))); }
	else if (Name == TEXT("atti")) { ToggleAtti(); }
	else if (Name == TEXT("stickmode")) { SetStickMode(static_cast<EDroneStickMode>(FMath::Clamp(Value, 0, 2))); }
	else if (Name == TEXT("camera")) { CycleCameraView(); }
	else if (Name == TEXT("pause")) { PauseFlight(); }
	else if (Name == TEXT("reset")) { ResetDrone(); }
	// Weather and landscape are shared by everyone, so only drone 1 (the instructor's) may change them.
	else if (Name == TEXT("wind") && PhoneSlot == 0) { CycleWind(); }
	else if (Name == TEXT("scene") && PhoneSlot == 0)
	{
		if (ADroneSimGameMode* GameMode = GetWorld()->GetAuthGameMode<ADroneSimGameMode>())
		{
			GameMode->CycleScene();   // calls back into SetSpawnPoint
		}
	}
}

void ADronePawn::UpdateStickGestures(float DeltaSeconds)
{
	// Combination stick command: both sticks down and inward (or outward) starts / stops the motors.
	const FDroneSticks& S = RawSticks;
	const bool bBothDown = S.LeftY < -0.8f && S.RightY < -0.8f;
	const bool bInward = S.LeftX > 0.8f && S.RightX < -0.8f;
	const bool bOutward = S.LeftX < -0.8f && S.RightX > 0.8f;

	if (bBothDown && (bInward || bOutward))
	{
		CscHoldTime += DeltaSeconds;
		if (CscHoldTime >= CscHoldSeconds && !bCscLatched)
		{
			bCscLatched = true;
			if (State == EDroneState::MotorsOff && bOnGround)
			{
				StartMotors();
			}
			else if (State == EDroneState::Idle)
			{
				StopMotors();
			}
		}
	}
	else
	{
		CscHoldTime = 0.0;
		bCscLatched = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------

void ADronePawn::StartMotors()
{
	HomeLocation = GetActorLocation();   // the home point is recorded at take-off, as on the real thing
	State = EDroneState::Idle;
	bBatteryRthDone = false;
}

void ADronePawn::StopMotors()
{
	State = EDroneState::MotorsOff;
	ThrustAccel = 0.0;
	ThrottleDownGroundTime = 0.0;
}

void ADronePawn::ToggleTakeoffLand()
{
	switch (State)
	{
	case EDroneState::MotorsOff:
		if (bOnGround && BatteryPercent > CriticalBatteryPercent)
		{
			StartMotors();
			State = EDroneState::TakingOff;
		}
		break;
	case EDroneState::Idle:
		State = EDroneState::TakingOff;
		break;
	case EDroneState::TakingOff:
	case EDroneState::Flying:
	case EDroneState::ReturningHome:
		State = EDroneState::Landing;
		break;
	case EDroneState::Landing:
		if (BatteryPercent > CriticalBatteryPercent)
		{
			State = EDroneState::Flying;
		}
		break;
	default:
		break;
	}
}

void ADronePawn::ToggleReturnToHome()
{
	if (State == EDroneState::Flying)
	{
		// Climb to the RTH altitude first unless we are already close to home.
		const double Height = GetHeightM();
		RthTargetHeightM = GetDistanceM() > 20.f ? FMath::Max<double>(Height, RthAltitudeM) : Height;
		State = EDroneState::ReturningHome;
	}
	else if (State == EDroneState::ReturningHome)
	{
		State = EDroneState::Flying;
	}
}

void ADronePawn::SetFlightMode(EDroneFlightMode NewMode)
{
	FlightMode = NewMode;
}

void ADronePawn::ToggleAtti()
{
	bAtti = !bAtti;
}

void ADronePawn::SetStickMode(EDroneStickMode NewMode)
{
	StickMode = NewMode;
}

void ADronePawn::CycleCameraView()
{
	CameraView = static_cast<EDroneCameraView>((static_cast<int32>(CameraView) + 1) % 3);
}

void ADronePawn::CycleWind()
{
	if (ADroneSimGameMode* GameMode = GetWorld()->GetAuthGameMode<ADroneSimGameMode>())
	{
		GameMode->CycleWeather();   // comes back to every drone through SetWeather
		return;
	}
	WindPreset = (WindPreset + 1) % static_cast<int32>(UE_ARRAY_COUNT(WindPresets));
	WindSpeed = WindPresets[WindPreset];
}

void ADronePawn::SetWeather(float InWindSpeed, float InWindHeadingDeg, float InGustFraction, float InTemperatureC, bool bInRain)
{
	WindSpeed = InWindSpeed;
	WindHeadingDeg = InWindHeadingDeg;
	GustFraction = InGustFraction;
	TemperatureC = InTemperatureC;
	bRain = bInRain;
}

void ADronePawn::PauseFlight()
{
	if (State == EDroneState::TakingOff || State == EDroneState::Landing || State == EDroneState::ReturningHome)
	{
		if (BatteryPercent > CriticalBatteryPercent && !bOnGround)
		{
			State = EDroneState::Flying;
		}
	}
	if (State == EDroneState::Flying)
	{
		PauseBrakeTime = 1.5;   // ignore the sticks and brake hard, even in ATTI
	}
}

void ADronePawn::SetPhoneSlot(int32 Slot)
{
	PhoneSlot = FMath::Clamp(Slot, 0, 3);

	// Body colour tells the drones apart: grey, orange, blue, yellow.
	static const FLinearColor SlotColors[] =
	{
		FLinearColor(0.18f, 0.19f, 0.2f), FLinearColor(0.9f, 0.3f, 0.03f), FLinearColor(0.05f, 0.25f, 0.9f), FLinearColor(0.9f, 0.75f, 0.05f)
	};
	if (Parts.Num() > 0 && Parts[0])
	{
		if (UMaterialInstanceDynamic* Material = Parts[0]->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), SlotColors[PhoneSlot]);
		}
	}

	FVector Spawn;
	float Fence = 0.f;
	const ADroneSimGameMode* GameMode = GetWorld()->GetAuthGameMode<ADroneSimGameMode>();
	if (GameMode && GameMode->GetSceneSpawn(PhoneSlot, Spawn, Fence))
	{
		SetSpawnPoint(Spawn, Fence);
	}
	else
	{
		// Custom map: step aside from the player start and let Tick find the ground again.
		SetActorLocation(GetActorLocation() + FVector(0.0, 250.0 * PhoneSlot, 0.0), false);
		bPlaced = false;
		PlaceTimer = 0.f;
	}
}

void ADronePawn::SetSpawnPoint(const FVector& GroundLocation, float InFenceRadiusM)
{
	SpawnLocation = GroundLocation + FVector(0.0, 0.0, CollisionRadiusCm + 2.0);
	FenceRadiusM = InFenceRadiusM;
	bPlaced = true;

	// The pilot stands 3 m behind the take-off point, on whatever ground is there.
	const FVector Behind = SpawnLocation + FRotator(0.0, SpawnYawDeg, 0.0).RotateVector(FVector(-300.0, 0.0, 0.0));
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DronePilotTrace), false, this);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Behind + FVector(0.0, 0.0, 5000.0), Behind - FVector(0.0, 0.0, 5000.0), ECC_Visibility, Params);
	PilotEyeLocation = (bHit ? Hit.ImpactPoint : Behind) + FVector(0.0, 0.0, 170.0);

	ResetDrone();
}

void ADronePawn::ResetDrone()
{
	SetActorLocation(SpawnLocation, false);
	HomeLocation = SpawnLocation;
	State = EDroneState::MotorsOff;
	Velocity = FVector::ZeroVector;
	TiltTan = FVector2D::ZeroVector;
	YawDeg = SpawnYawDeg;
	YawRateDeg = 0.0;
	ThrustAccel = 0.0;
	bOnGround = true;
	bAtMaxHeight = false;
	bAtFence = false;
	bBatteryRthDone = false;
	BatteryPercent = 100.f;
	GimbalPitch = 0.f;
	ThrottleDownGroundTime = 0.0;
}

// ---------------------------------------------------------------------------------------------
// Flight model
// ---------------------------------------------------------------------------------------------

const FDroneModeLimits& ADronePawn::GetLimits() const
{
	switch (FlightMode)
	{
	case EDroneFlightMode::Cine: return CineLimits;
	case EDroneFlightMode::Sport: return SportLimits;
	default: return NormalLimits;
	}
}

FVector ADronePawn::GetWindVelocity() const
{
	if (WindSpeed <= 0.f)
	{
		return FVector::ZeroVector;
	}

	// Wind shear: about half strength at the ground, full strength from 40 m up.
	const double Shear = 0.5 + 0.5 * FMath::Clamp<double>(GetHeightM() / 40.0, 0.0, 1.0);
	const double Mean = WindSpeed * Shear;
	const double HeadingRad = FMath::DegreesToRadians(static_cast<double>(WindHeadingDeg));
	const FVector Along(FMath::Cos(HeadingRad), FMath::Sin(HeadingRad), 0.0);
	const FVector Across(-Along.Y, Along.X, 0.0);

	const float T = static_cast<float>(SimTime);
	const double GustAlong = FMath::PerlinNoise1D(T * 0.35f);
	const double GustAcross = FMath::PerlinNoise1D(T * 0.35f + 37.1f);
	const double GustUp = FMath::PerlinNoise1D(T * 0.5f + 91.7f);

	return Along * (Mean * (1.0 + GustFraction * GustAlong))
		+ Across * (Mean * GustFraction * 0.6 * GustAcross)
		+ FVector(0.0, 0.0, Mean * GustFraction * 0.25 * GustUp);
}

void ADronePawn::Simulate(double Dt)
{
	// ---- Motors off or crashed: a falling object.
	if (!AreMotorsRunning())
	{
		ThrustAccel = 0.0;
		if (bOnGround)
		{
			Velocity = FVector::ZeroVector;
			if (State == EDroneState::MotorsOff)
			{
				TiltTan = FVector2D::ZeroVector;
			}
			return;
		}
		Velocity.Z -= Gravity * Dt;
		Velocity -= Velocity * FMath::Min<double>(1.0, 0.3 * Dt);
		MoveAndCollide(Dt);
		return;
	}

	// ---- Idling on the ground until the pilot raises the throttle.
	if (State == EDroneState::Idle)
	{
		Velocity = FVector::ZeroVector;
		TiltTan = FVector2D::ZeroVector;
		ThrustAccel = 0.2 * Gravity;
		if (Axes.Throttle <= 0.15)
		{
			return;
		}
		State = EDroneState::Flying;
	}

	const FDroneModeLimits& Limits = GetLimits();
	const double YawRad = FMath::DegreesToRadians(YawDeg);
	const FVector2D Forward(FMath::Cos(YawRad), FMath::Sin(YawRad));
	const FVector2D Right(-Forward.Y, Forward.X);
	const FVector Location = GetActorLocation();
	const double Height = (Location.Z - HomeLocation.Z) / 100.0;
	const FVector2D ToHome((HomeLocation.X - Location.X) / 100.0, (HomeLocation.Y - Location.Y) / 100.0);
	const double HomeDistance = ToHome.Size();

	// ---- What the flight controller is asked to do
	FVector2D VelocityCommand = FVector2D::ZeroVector;   // m/s, world frame
	double ClimbCommand = 0.0;                           // m/s
	double YawRateCommand = 0.0;                         // deg/s
	double MaxTiltDeg = Limits.MaxTiltDeg;
	bool bDirectTilt = false;

	const FVector2D StickDirection = Forward * Axes.Pitch + Right * Axes.Roll;

	switch (State)
	{
	case EDroneState::TakingOff:
		ClimbCommand = FMath::Clamp<double>((HoverHeightM - Height) * 2.0, 0.3, 1.5);
		if (Height >= HoverHeightM - 0.05)
		{
			State = EDroneState::Flying;
		}
		break;

	case EDroneState::Landing:
		VelocityCommand = StickDirection * FMath::Min<double>(Limits.MaxSpeed, 3.0);
		YawRateCommand = Axes.Yaw * Limits.MaxYawRateDeg;
		ClimbCommand = GroundDistanceM > 3.0 ? -2.5 : -0.6;
		if (Axes.Throttle > 0.5 && BatteryPercent > CriticalBatteryPercent)
		{
			State = EDroneState::Flying;   // pilot cancels the landing
		}
		break;

	case EDroneState::ReturningHome:
		MaxTiltDeg = NormalLimits.MaxTiltDeg;
		if (HomeDistance <= 0.5)
		{
			State = EDroneState::Landing;
		}
		else if (Height < RthTargetHeightM - 0.5)
		{
			ClimbCommand = 3.0;   // climb straight up to the RTH altitude first
		}
		else
		{
			const double Speed = FMath::Clamp<double>(HomeDistance * 0.6, 0.4, 10.0);
			VelocityCommand = ToHome / HomeDistance * Speed;
			ClimbCommand = FMath::Clamp<double>(RthTargetHeightM - Height, -1.0, 1.0);
			if (HomeDistance > 3.0)
			{
				const double BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(ToHome.Y, ToHome.X));
				YawRateCommand = FMath::Clamp<double>(FMath::FindDeltaAngleDegrees(YawDeg, BearingDeg) * 2.0, -90.0, 90.0);
			}
		}
		break;

	case EDroneState::Flying:
	default:
		VelocityCommand = StickDirection * Limits.MaxSpeed;
		ClimbCommand = Axes.Throttle >= 0.0 ? Axes.Throttle * Limits.MaxAscent : Axes.Throttle * Limits.MaxDescent;
		YawRateCommand = Axes.Yaw * Limits.MaxYawRateDeg;
		bDirectTilt = bAtti;
		if (PauseBrakeTime > 0.0)
		{
			PauseBrakeTime -= Dt;
			VelocityCommand = FVector2D::ZeroVector;
			ClimbCommand = 0.0;
			YawRateCommand = 0.0;
			bDirectTilt = false;
		}

		// Holding the throttle down while sitting on the ground stops the motors.
		if (bOnGround && Axes.Throttle < -0.8)
		{
			ThrottleDownGroundTime += Dt;
			if (ThrottleDownGroundTime >= LandHoldSeconds)
			{
				StopMotors();
				return;
			}
		}
		else
		{
			ThrottleDownGroundTime = 0.0;
		}
		break;
	}

	// Flight area limit: outside it, only commands that bring the drone back are accepted.
	bAtFence = false;
	if (FenceRadiusM > 0.0)
	{
		const FVector2D FromSpawn((Location.X - SpawnLocation.X) / 100.0, (Location.Y - SpawnLocation.Y) / 100.0);
		const double SpawnDistance = FromSpawn.Size();
		if (SpawnDistance > FenceRadiusM)
		{
			const FVector2D Outward = FromSpawn / SpawnDistance;
			const double OutwardSpeed = FVector2D::DotProduct(VelocityCommand, Outward);
			if (OutwardSpeed > 0.0)
			{
				VelocityCommand -= Outward * OutwardSpeed;
			}
			VelocityCommand -= Outward * FMath::Min<double>(3.0, (SpawnDistance - FenceRadiusM) * 0.5);
			bDirectTilt = false;   // position control takes over, even in ATTI
			bAtFence = true;
		}
	}

	// Height ceiling and landing protection (slow descent close to the ground).
	if (ClimbCommand > 0.0 && Height >= MaxHeightM)
	{
		ClimbCommand = 0.0;
		bAtMaxHeight = true;
	}
	else if (Height < MaxHeightM - 1.0)
	{
		bAtMaxHeight = false;
	}
	if (GroundDistanceM < 1.5)
	{
		ClimbCommand = FMath::Max<double>(ClimbCommand, -1.0);
	}

	// ---- Aerodynamics
	const FVector AirVelocity = Velocity - GetWindVelocity();
	const FVector DragAccel = -AirVelocity * (LinearDrag + QuadraticDrag * AirVelocity.Size());

	// ---- Attitude: lean so the horizontal part of the thrust gives the wanted acceleration
	const double MaxTan = FMath::Tan(FMath::DegreesToRadians(MaxTiltDeg));
	FVector2D AccelCommand;
	if (bDirectTilt)
	{
		// ATTI: sticks set the lean angle. No velocity feedback, so wind and momentum carry the drone.
		AccelCommand = StickDirection * (Gravity * MaxTan);
	}
	else
	{
		// GPS: close the loop on ground velocity and lean into the wind to cancel drag.
		AccelCommand = (VelocityCommand - FVector2D(Velocity.X, Velocity.Y)) * VelocityGain - FVector2D(DragAccel.X, DragAccel.Y);
	}
	FVector2D TargetTan = AccelCommand / Gravity;
	const double TargetTanSize = TargetTan.Size();
	if (TargetTanSize > MaxTan)
	{
		TargetTan *= MaxTan / TargetTanSize;
	}
	TiltTan += (TargetTan - TiltTan) * (1.0 - FMath::Exp(-Dt / FMath::Max<double>(AttitudeTimeConstant, 0.01)));

	YawRateDeg += (YawRateCommand - YawRateDeg) * (1.0 - FMath::Exp(-Dt / 0.1));
	YawDeg = FRotator::NormalizeAxis(YawDeg + YawRateDeg * Dt);

	// ---- Thrust: altitude hold picks whatever thrust gives the wanted vertical acceleration
	const FVector Up = FVector(TiltTan.X, TiltTan.Y, 1.0).GetSafeNormal();
	const double VerticalAccelCommand = FMath::Clamp<double>((ClimbCommand - Velocity.Z) * 4.0, -6.0, 6.0) - DragAccel.Z;
	ThrustAccel = FMath::Clamp<double>((Gravity + VerticalAccelCommand) / Up.Z, 0.1 * Gravity, MaxThrustToWeight * Gravity);

	Velocity += (Up * ThrustAccel + FVector(0.0, 0.0, -Gravity) + DragAccel) * Dt;

	if (bOnGround)
	{
		// Skids on the ground: kill sideways motion quickly.
		const double Friction = FMath::Exp(-8.0 * Dt);
		Velocity.X *= Friction;
		Velocity.Y *= Friction;
	}

	MoveAndCollide(Dt);
}

void ADronePawn::MoveAndCollide(double Dt)
{
	const FVector Delta = Velocity * 100.0 * Dt;
	FHitResult Hit;
	SetActorLocation(GetActorLocation() + Delta, true, &Hit);

	if (!Hit.bBlockingHit)
	{
		if (Velocity.Z > 0.05 || Delta.Z < -0.01)
		{
			bOnGround = false;
		}
		return;
	}

	const FVector Normal = Hit.ImpactNormal;
	const double ImpactSpeed = -FVector::DotProduct(Velocity, Normal);
	const bool bFloor = Normal.Z > 0.7;

	if (AreMotorsRunning() && ImpactSpeed > (bFloor ? CrashSpeedGround : CrashSpeedObstacle))
	{
		Crash(Normal, ImpactSpeed);
		return;
	}

	if (ImpactSpeed > 0.0)
	{
		Velocity += Normal * ImpactSpeed;   // remove the part of the velocity going into the surface
	}

	if (bFloor)
	{
		bOnGround = true;
		if (!AreMotorsRunning())
		{
			Velocity = FVector::ZeroVector;
		}
		else if (State == EDroneState::Landing)
		{
			StopMotors();
		}
	}
	else
	{
		// Slide along walls with what is left of this step.
		const FVector Remaining = FVector::VectorPlaneProject(Delta * (1.0 - Hit.Time), Normal);
		SetActorLocation(GetActorLocation() + Remaining, true);
	}
}

void ADronePawn::Crash(const FVector& Normal, double ImpactSpeed)
{
	State = EDroneState::Crashed;
	ThrustAccel = 0.0;
	bOnGround = false;
	Velocity += Normal * (ImpactSpeed * 1.3);   // small bounce off whatever we hit
	CrashSpin = FRotator(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-200.f, 200.f), FMath::FRandRange(-400.f, 400.f));
}

// ---------------------------------------------------------------------------------------------
// Battery, warnings, visuals, telemetry
// ---------------------------------------------------------------------------------------------

void ADronePawn::UpdateBatteryAndWarnings(float DeltaSeconds)
{
	if (AreMotorsRunning())
	{
		// Power rises roughly with thrust^1.5; idling on the ground costs little.
		const double Load = State == EDroneState::Idle ? 0.2 : FMath::Pow(ThrustAccel / Gravity, 1.5);
		// Cold batteries deliver less: 2 % more drain per degree below 15 C (about +40 % at -5 C).
		const double Cold = 1.0 + 0.02 * FMath::Max(0.f, 15.f - TemperatureC);
		const double PercentPerSecond = Cold * 100.0 / (FMath::Max(HoverFlightTimeMinutes, 0.1f) * 60.0);
		BatteryPercent = FMath::Max(0.f, BatteryPercent - static_cast<float>(PercentPerSecond * Load * DeltaSeconds));

		const bool bAirborne = State == EDroneState::Flying || State == EDroneState::ReturningHome || State == EDroneState::TakingOff;
		if (BatteryPercent <= 0.f)
		{
			StopMotors();          // empty battery: it falls out of the sky
			bOnGround = false;
		}
		else if (BatteryPercent <= CriticalBatteryPercent && bAirborne)
		{
			State = EDroneState::Landing;
		}
		else if (BatteryPercent <= RthBatteryPercent && !bBatteryRthDone && State == EDroneState::Flying)
		{
			bBatteryRthDone = true;
			if (GetDistanceM() > 10.f)
			{
				ToggleReturnToHome();
			}
		}
	}

	if (State == EDroneState::Crashed) { WarningText = TEXT("CRASHED - press R / Reset"); }
	else if (BatteryPercent <= 0.f) { WarningText = TEXT("BATTERY EMPTY - press R / Reset"); }
	else if (BatteryPercent <= CriticalBatteryPercent) { WarningText = TEXT("CRITICAL BATTERY - LANDING"); }
	else if (State == EDroneState::ReturningHome) { WarningText = TEXT("RETURNING TO HOME"); }
	else if (BatteryPercent <= LowBatteryPercent) { WarningText = TEXT("LOW BATTERY - return to home"); }
	else if (bAtFence) { WarningText = TEXT("EDGE OF FLIGHT AREA"); }
	else if (bAtMaxHeight) { WarningText = TEXT("MAX ALTITUDE REACHED"); }
	else if (GetDistanceM() > VlosWarningDistanceM) { WarningText = TEXT("BEYOND VISUAL LINE OF SIGHT"); }
	else if (bAtti) { WarningText = TEXT("ATTI MODE - no position hold"); }
	else if (bRain) { WarningText = TEXT("RAIN - land as soon as possible"); }
	else if (WindSpeed >= 10.f) { WarningText = TEXT("STRONG WIND"); }
	else if (TemperatureC < 0.f && AreMotorsRunning()) { WarningText = TEXT("COLD - battery drains faster"); }
	else { WarningText.Reset(); }
}

void ADronePawn::UpdateVisuals(float DeltaSeconds)
{
	if (State == EDroneState::Crashed)
	{
		if (!bOnGround)
		{
			AddActorLocalRotation(CrashSpin * DeltaSeconds);
		}
	}
	else
	{
		const double YawRad = FMath::DegreesToRadians(YawDeg);
		const FVector Up = FVector(TiltTan.X, TiltTan.Y, 1.0).GetSafeNormal();
		const FVector Forward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.0);
		SetActorRotation(FRotationMatrix::MakeFromZX(Up, Forward).Rotator());
	}

	if (AreMotorsRunning())
	{
		const float Rate = 1500.f + 4000.f * static_cast<float>(FMath::Sqrt(FMath::Max<double>(ThrustAccel, 0.0) / Gravity));
		PropAngle = FMath::Fmod(PropAngle + Rate * DeltaSeconds, 360.f);
	}
	for (int32 i = 0; i < Props.Num(); ++i)
	{
		if (Props[i])
		{
			// Diagonal pairs spin the same way.
			const float Direction = (i == 0 || i == 3) ? 1.f : -1.f;
			Props[i]->SetRelativeRotation(FRotator(0.f, Direction * PropAngle + i * 45.f, 0.f));
		}
	}
}

void ADronePawn::SendPhoneTelemetry(float DeltaSeconds)
{
	TelemetryTimer += DeltaSeconds;
	if (TelemetryTimer < 0.1f)
	{
		return;
	}
	TelemetryTimer = 0.f;

	const UGameInstance* GameInstance = GetGameInstance();
	UPhoneControllerSubsystem* Phone = GameInstance ? GameInstance->GetSubsystem<UPhoneControllerSubsystem>() : nullptr;
	if (!Phone || !(Phone->IsSlotActive(PhoneSlot) || (PhoneSlot == 0 && Phone->GetNumPhones() > 0)))
	{
		return;
	}

	const ADroneSimGameMode* GameMode = GetWorld()->GetAuthGameMode<ADroneSimGameMode>();
	const FString SceneName = GameMode ? GameMode->GetSceneName() : FString();
	const FString Weather = GameMode ? GameMode->GetWeatherLabel() : FString::Printf(TEXT("Wind %.0f m/s"), WindSpeed);

	// state|mode|atti|stickMode|height|distance|hSpeed|vSpeed|battery|camera|weather|scene|warning
	Phone->SendTelemetry(PhoneSlot, FString::Printf(TEXT("%d|%d|%d|%d|%.1f|%.0f|%.1f|%.1f|%.0f|%d|%s|%s|%s"),
		static_cast<int32>(State), static_cast<int32>(FlightMode), bAtti ? 1 : 0, static_cast<int32>(StickMode),
		GetHeightM(), GetDistanceM(), GetHorizontalSpeed(), GetVerticalSpeed(), BatteryPercent,
		static_cast<int32>(CameraView), *Weather, SceneName.IsEmpty() ? TEXT("-") : *SceneName, *WarningText));
}

float ADronePawn::GetHeightM() const
{
	return static_cast<float>((GetActorLocation().Z - HomeLocation.Z) / 100.0);
}

float ADronePawn::GetDistanceM() const
{
	return static_cast<float>(FVector::Dist2D(GetActorLocation(), HomeLocation) / 100.0);
}

float ADronePawn::GetHorizontalSpeed() const
{
	return static_cast<float>(Velocity.Size2D());
}

float ADronePawn::GetVerticalSpeed() const
{
	return static_cast<float>(Velocity.Z);
}

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------

void ADronePawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	const FVector DroneLocation = GetActorLocation();
	const FRotator YawRotation(0.0, YawDeg, 0.0);

	OutResult.FOV = CameraFov;

	switch (CameraView)
	{
	case EDroneCameraView::Chase:
	{
		OutResult.Location = DroneLocation + YawRotation.RotateVector(FVector(-350.0, 0.0, 120.0));
		OutResult.Rotation = (DroneLocation - OutResult.Location).Rotation();
		break;
	}
	case EDroneCameraView::Pilot:
	{
		// Standing at the take-off point watching the aircraft, as in real line-of-sight flying.
		OutResult.Location = PilotEyeLocation;
		OutResult.Rotation = (DroneLocation - PilotEyeLocation).Rotation();
		OutResult.FOV = 70.f;
		if (bPilotViewZoomAssist)
		{
			const float DistanceM = static_cast<float>(FVector::Dist(DroneLocation, PilotEyeLocation) / 100.0);
			OutResult.FOV = FMath::GetMappedRangeValueClamped(FVector2f(15.f, 150.f), FVector2f(70.f, 25.f), DistanceM);
		}
		break;
	}
	case EDroneCameraView::FPV:
	default:
	{
		// Stabilised gimbal: follows the drone's heading, ignores its pitch and roll.
		OutResult.Location = DroneLocation + YawRotation.RotateVector(FVector(16.0, 0.0, -4.0));
		OutResult.Rotation = FRotator(GimbalPitch, YawDeg, 0.0);
		break;
	}
	}
}
