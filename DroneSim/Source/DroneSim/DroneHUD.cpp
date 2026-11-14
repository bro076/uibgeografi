#include "DroneHUD.h"

#include "DronePawn.h"
#include "DroneSimGameMode.h"
#include "PhoneControllerSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace
{
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(0.75f, 0.78f, 0.8f, 1.f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.45f);
	const FLinearColor Good(0.25f, 0.9f, 0.4f, 1.f);
	const FLinearColor Warn(1.f, 0.75f, 0.1f, 1.f);
	const FLinearColor Bad(1.f, 0.25f, 0.2f, 1.f);

	const TCHAR* StateName(EDroneState State)
	{
		switch (State)
		{
		case EDroneState::MotorsOff: return TEXT("Motors off");
		case EDroneState::Idle: return TEXT("Ready to take off");
		case EDroneState::TakingOff: return TEXT("Taking off");
		case EDroneState::Flying: return TEXT("In flight");
		case EDroneState::Landing: return TEXT("Landing");
		case EDroneState::ReturningHome: return TEXT("Returning home");
		case EDroneState::Crashed: return TEXT("Crashed");
		default: return TEXT("");
		}
	}

	const TCHAR* ModeName(EDroneFlightMode Mode)
	{
		switch (Mode)
		{
		case EDroneFlightMode::Cine: return TEXT("CINE");
		case EDroneFlightMode::Sport: return TEXT("SPORT");
		default: return TEXT("NORMAL");
		}
	}

	const TCHAR* ViewName(EDroneCameraView View)
	{
		switch (View)
		{
		case EDroneCameraView::Chase: return TEXT("Chase view");
		case EDroneCameraView::Pilot: return TEXT("Pilot view (line of sight)");
		default: return TEXT("Camera view");
		}
	}

	// Labels for [stick mode][0 = left, 1 = right]
	const TCHAR* StickLabels[3][2] =
	{
		{ TEXT("Pitch / Yaw"), TEXT("Throttle / Roll") },
		{ TEXT("Throttle / Yaw"), TEXT("Pitch / Roll") },
		{ TEXT("Throttle / Roll"), TEXT("Pitch / Yaw") },
	};
}

void ADroneHUD::DrawHUD()
{
	Super::DrawHUD();

	const ADronePawn* Drone = Cast<ADronePawn>(GetOwningPawn());
	if (!Canvas || !Drone || !GEngine)
	{
		return;
	}

	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (PC->WasInputKeyJustPressed(EKeys::F1)) { bShowHelp = !bShowHelp; }
		if (PC->WasInputKeyJustPressed(EKeys::Q)) { bShowQr = !bShowQr; }
	}

	// With several drones the screen is split; shared panels (pairing, help) go on the first player's view only.
	const APlayerController* Owner = GetOwningPlayerController();
	const bool bPrimary = !Owner || Owner->IsPrimaryPlayer();

	if (Drone->IsInRain())
	{
		DrawRain();
	}

	UFont* Font = GEngine->GetMediumFont();
	const float W = static_cast<float>(Canvas->SizeX);
	const float H = static_cast<float>(Canvas->SizeY);
	const float S = H / 1080.f;          // layout scale
	const float T = 1.5f * S;            // text scale
	const float Line = 26.f * S;
	const float Margin = 24.f * S;

	// ---- Top left: state, flight mode, view
	const ADroneSimGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ADroneSimGameMode>() : nullptr;
	const FString SceneName = GameMode ? GameMode->GetSceneName() : FString();
	DrawRect(Panel, Margin, Margin, 430.f * S, Line * (SceneName.IsEmpty() ? 3.f : 4.f) + 16.f * S);
	if (!SceneName.IsEmpty())
	{
		DrawText(SceneName, Dim, Margin + 10.f * S, Margin + 8.f * S + Line * 3.f, Font, T);
	}
	const int32 StickModeIndex = FMath::Clamp(static_cast<int32>(Drone->GetStickMode()), 0, 2);
	DrawText(FString::Printf(TEXT("Drone %d:  %s  -  %s%s"), Drone->GetPhoneSlot() + 1, StateName(Drone->GetDroneState()), ModeName(Drone->GetFlightMode()), Drone->IsAtti() ? TEXT(" / ATTI") : TEXT(" / GPS")),
		Drone->GetDroneState() == EDroneState::Crashed ? Bad : White, Margin + 10.f * S, Margin + 8.f * S, Font, T);
	DrawText(FString::Printf(TEXT("%s   Gimbal %.0f deg"), ViewName(Drone->GetCameraView()), Drone->GetGimbalPitch()),
		Dim, Margin + 10.f * S, Margin + 8.f * S + Line, Font, T);
	DrawText(FString::Printf(TEXT("Stick mode %d   %s   %.0f C"), StickModeIndex + 1,
			GameMode ? *GameMode->GetWeatherLabel() : *FString::Printf(TEXT("Wind %.0f m/s"), Drone->GetWindSpeed()), Drone->GetTemperatureC()),
		Dim, Margin + 10.f * S, Margin + 8.f * S + Line * 2.f, Font, T);

	// ---- Top centre: battery
	const float Battery = Drone->GetBatteryPercent();
	const FLinearColor BatteryColor = Battery > 25.f ? Good : (Battery > 10.f ? Warn : Bad);
	const float BarW = 220.f * S;
	const float BarX = (W - BarW) * 0.5f;
	DrawRect(Panel, BarX - 6.f * S, Margin, BarW + 12.f * S, 34.f * S);
	DrawRect(BatteryColor, BarX, Margin + 6.f * S, BarW * Battery / 100.f, 22.f * S);
	DrawText(FString::Printf(TEXT("%.0f%%"), Battery), White, BarX + BarW * 0.5f - 18.f * S, Margin + 6.f * S, Font, T);

	// ---- Warning banner
	const FString Warning = Drone->GetWarningText();
	if (!Warning.IsEmpty())
	{
		const float BannerW = 560.f * S;
		DrawRect(FLinearColor(0.55f, 0.05f, 0.02f, 0.75f), (W - BannerW) * 0.5f, Margin + 48.f * S, BannerW, 40.f * S);
		DrawText(Warning, White, (W - BannerW) * 0.5f + 16.f * S, Margin + 56.f * S, Font, T);
	}

	// ---- Bottom left: flight telemetry (same quantities as the DJI Fly app)
	const float TelemetryY = H - Margin - Line * 2.f - 16.f * S;
	DrawRect(Panel, Margin, TelemetryY, 430.f * S, Line * 2.f + 16.f * S);
	DrawText(FString::Printf(TEXT("H %6.1f m      D %6.0f m"), Drone->GetHeightM(), Drone->GetDistanceM()),
		White, Margin + 10.f * S, TelemetryY + 8.f * S, Font, T);
	DrawText(FString::Printf(TEXT("H.S %4.1f m/s    V.S %+4.1f m/s"), Drone->GetHorizontalSpeed(), Drone->GetVerticalSpeed()),
		White, Margin + 10.f * S, TelemetryY + 8.f * S + Line, Font, T);

	// ---- Bottom centre: what the sticks are doing (useful when coaching a student)
	const FDroneSticks& Sticks = Drone->GetRawSticks();
	const float StickHalf = 55.f * S;
	const float StickY = H - Margin - StickHalf - Line;
	DrawStick(W * 0.5f - 90.f * S, StickY, StickHalf, Sticks.LeftX, Sticks.LeftY, StickLabels[StickModeIndex][0], T * 0.8f);
	DrawStick(W * 0.5f + 90.f * S, StickY, StickHalf, Sticks.RightX, Sticks.RightY, StickLabels[StickModeIndex][1], T * 0.8f);

	// ---- Top right: phone pairing
	FString Url;
	int32 NumPhones = 0;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UPhoneControllerSubsystem* Phone = GameInstance->GetSubsystem<UPhoneControllerSubsystem>())
		{
			Url = Phone->GetControllerUrl();
			NumPhones = Phone->GetNumPhones();
		}
	}
	if (bPrimary)
	{
		DrawPairingPanel(S, T, Url, NumPhones);
	}

	// ---- Help
	if (bShowHelp && bPrimary)
	{
		static const TCHAR* HelpLines[] =
		{
			TEXT("F1  show / hide this help        Q  show / hide QR code       F2  next landscape"),
			TEXT("Left stick  W A S D              Right stick  arrow keys      (or a gamepad / phone)"),
			TEXT("T  take off / land               H  return to home            R  reset"),
			TEXT("1 2 3  Cine / Normal / Sport     4  ATTI (no GPS hold)        M  stick mode"),
			TEXT("C  camera view                   PgUp PgDn  gimbal tilt       G  weather      Space  pause / brake"),
			TEXT("Start motors by hand: hold both sticks down and inward, then raise the throttle"),
		};
		const int32 NumLines = UE_ARRAY_COUNT(HelpLines);
		const float HelpW = 860.f * S;
		const float HelpX = W - Margin - HelpW;
		const float HelpY = H - Margin - Line * NumLines - 16.f * S;
		DrawRect(Panel, HelpX, HelpY, HelpW, Line * NumLines + 16.f * S);
		for (int32 i = 0; i < NumLines; ++i)
		{
			DrawText(HelpLines[i], Dim, HelpX + 10.f * S, HelpY + 8.f * S + Line * i, Font, T * 0.85f);
		}
	}
}

void ADroneHUD::DrawRain()
{
	// Streaks across the view. Cheap, but it makes "it has started raining" impossible to miss.
	const float W = static_cast<float>(Canvas->SizeX);
	const float H = static_cast<float>(Canvas->SizeY);
	const float Length = H * 0.06f;
	const FLinearColor Streak(0.8f, 0.85f, 0.9f, 0.22f);
	for (int32 i = 0; i < 90; ++i)
	{
		const float X = FMath::FRandRange(0.f, W);
		const float Y = FMath::FRandRange(-Length, H);
		DrawLine(X, Y, X - Length * 0.25f, Y + Length, Streak, 1.f);
	}
}

void ADroneHUD::DrawStick(float CenterX, float CenterY, float HalfSize, float X, float Y, const FString& Label, float TextScale)
{
	DrawRect(Panel, CenterX - HalfSize, CenterY - HalfSize, HalfSize * 2.f, HalfSize * 2.f);
	DrawLine(CenterX - HalfSize, CenterY, CenterX + HalfSize, CenterY, FLinearColor(1.f, 1.f, 1.f, 0.25f));
	DrawLine(CenterX, CenterY - HalfSize, CenterX, CenterY + HalfSize, FLinearColor(1.f, 1.f, 1.f, 0.25f));

	const float Dot = HalfSize * 0.16f;
	const float DotX = CenterX + X * (HalfSize - Dot);
	const float DotY = CenterY - Y * (HalfSize - Dot);   // screen Y grows downward
	DrawRect(Good, DotX - Dot, DotY - Dot, Dot * 2.f, Dot * 2.f);

	DrawText(Label, Dim, CenterX - HalfSize, CenterY + HalfSize + 4.f, GEngine->GetMediumFont(), TextScale);
}

void ADroneHUD::DrawPairingPanel(float Scale, float TextScale, const FString& Url, int32 NumPhones)
{
	const float W = static_cast<float>(Canvas->SizeX);
	const float Margin = 24.f * Scale;
	UFont* Font = GEngine->GetMediumFont();

	if (Url.IsEmpty())
	{
		DrawText(TEXT("Phone controller unavailable (port in use?)"), Warn, W - Margin - 420.f * Scale, Margin, Font, TextScale);
		return;
	}

	const FString Status = NumPhones > 0
		? FString::Printf(TEXT("%d phone%s connected"), NumPhones, NumPhones == 1 ? TEXT("") : TEXT("s"))
		: FString(TEXT("Phone controller: scan or open"));
	const float PanelW = 330.f * Scale;
	const float PanelX = W - Margin - PanelW;
	const bool bDrawQr = bShowQr && NumPhones == 0;

	if (Url != QrUrl)
	{
		QrUrl = Url;
		Qr = DroneQr::Encode(std::string(TCHAR_TO_UTF8(*Url)));
	}

	const float TextH = 60.f * Scale;
	const float QrSide = (bDrawQr && Qr.Size > 0) ? PanelW : 0.f;
	DrawRect(Panel, PanelX, Margin, PanelW, TextH + QrSide);
	DrawText(Status, NumPhones > 0 ? Good : White, PanelX + 10.f * Scale, Margin + 6.f * Scale, Font, TextScale * 0.9f);
	DrawText(Url, White, PanelX + 10.f * Scale, Margin + 30.f * Scale, Font, TextScale * 0.9f);

	if (QrSide > 0.f)
	{
		// White card with a 4-module quiet zone, then one rect per dark module.
		const int32 Cells = Qr.Size + 8;
		const float Cell = FMath::FloorToFloat((QrSide - 20.f * Scale) / Cells);
		const float Side = Cell * Cells;
		const float X0 = PanelX + (PanelW - Side) * 0.5f;
		const float Y0 = Margin + TextH + (QrSide - Side) * 0.5f;
		DrawRect(White, X0, Y0, Side, Side);
		for (int32 Y = 0; Y < Qr.Size; ++Y)
		{
			for (int32 X = 0; X < Qr.Size; ++X)
			{
				if (Qr.IsDark(X, Y))
				{
					DrawRect(FLinearColor::Black, X0 + (X + 4) * Cell, Y0 + (Y + 4) * Cell, Cell, Cell);
				}
			}
		}
	}
}
