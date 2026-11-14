#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "QrCode.h"
#include "DroneHUD.generated.h"

/** DJI Fly style telemetry overlay, stick position display and the phone pairing QR code. */
UCLASS()
class DRONESIM_API ADroneHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawStick(float CenterX, float CenterY, float HalfSize, float X, float Y, const FString& Label, float TextScale);
	void DrawPairingPanel(float Scale, float TextScale, const FString& Url, int32 NumPhones);
	void DrawRain();
	void DrawRace(const class ADroneSimGameMode* GameMode, const class ADronePawn* Drone, float Scale, float TextScale, bool bPrimary);

	bool bShowHelp = true;
	bool bShowQr = true;
	FString QrUrl;
	DroneQr::FQrMatrix Qr;
};
