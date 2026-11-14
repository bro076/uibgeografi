#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "DroneTypes.h"
#include "PhoneControllerSubsystem.generated.h"

class FSocket;
struct FPhoneClient;

/**
 * Tiny HTTP + WebSocket server that turns phones on the same Wi-Fi into drone remotes.
 *
 *  - GET /            serves Content/Web/controller.html (twin virtual sticks)
 *  - WebSocket (any path) carries text messages:
 *        phone -> sim   "s|leftX|leftY|rightX|rightY|gimbal"   stick state, ~30 Hz
 *                       "c|<command>[|arg]"                    button presses
 *        sim -> phone   "t|<P or S>|<telemetry fields>"        ~10 Hz
 *
 * One phone is the pilot; others are spectators until they send "c|claim".
 * Everything runs non-blocking on the game thread.
 */
UCLASS()
class DRONESIM_API UPhoneControllerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Sticks of the pilot phone. Returns false if no phone is actively sending. */
	bool GetPilotSticks(FDroneSticks& OutSticks) const;

	/** Moves queued button commands ("takeoff", "mode|2", ...) into OutCommands. */
	void ConsumeCommands(TArray<FString>& OutCommands);

	/** Sends telemetry to every connected phone. */
	void BroadcastTelemetry(const FString& Fields);

	/** e.g. http://192.168.1.20:8080 - empty if the server could not start. */
	const FString& GetControllerUrl() const { return ControllerUrl; }

	int32 GetNumPhones() const;

private:
	bool OnTick(float DeltaTime);
	void AcceptConnections();
	void PumpClient(FPhoneClient& Client);
	void HandleHttp(FPhoneClient& Client);
	void HandleFrames(FPhoneClient& Client);
	void HandleText(FPhoneClient& Client, const FString& Message);
	void QueueFrame(FPhoneClient& Client, uint8 Opcode, const uint8* Payload, int32 Length);
	void QueueText(FPhoneClient& Client, const FString& Text);
	void QueueRaw(FPhoneClient& Client, const FString& Text);
	void Flush(FPhoneClient& Client);
	void CloseClient(FPhoneClient& Client);
	FPhoneClient* FindPilot() const;
	static FString FindLocalAddress();

	FSocket* ListenSocket = nullptr;
	TArray<TSharedPtr<FPhoneClient>> Clients;
	TArray<FString> PendingCommands;
	FTSTicker::FDelegateHandle TickHandle;
	FString ControllerUrl;
	int32 Port = 8080;
	int32 NextClientId = 1;
	int32 PilotId = 0;
};
