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
 *        sim -> phone   "t|<P1..P4 or S>|<telemetry fields>"   ~10 Hz
 *
 * Each phone gets its own drone: the first four to connect take slots 0-3 (shown as
 * drones 1-4). Further phones spectate drone 1 and move into a slot when one frees up.
 * Everything runs non-blocking on the game thread.
 */
UCLASS()
class DRONESIM_API UPhoneControllerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static constexpr int32 MaxSlots = 4;

	/** True while a phone is connected in this slot (0-3). */
	bool IsSlotActive(int32 Slot) const;

	/** Sticks of the phone in a slot. Returns false if there is none or it stopped sending. */
	bool GetSticks(int32 Slot, FDroneSticks& OutSticks) const;

	/** Moves that phone's queued button commands ("takeoff", "mode|2", ...) into OutCommands. */
	void ConsumeCommands(int32 Slot, TArray<FString>& OutCommands);

	/** Sends a drone's telemetry to its phone. Slot 0's telemetry also goes to spectators. */
	void SendTelemetry(int32 Slot, const FString& Fields);

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
	FPhoneClient* FindSlot(int32 Slot) const;
	static FString FindLocalAddress();

	FSocket* ListenSocket = nullptr;
	TArray<TSharedPtr<FPhoneClient>> Clients;
	FTSTicker::FDelegateHandle TickHandle;
	FString ControllerUrl;
	int32 Port = 8080;
	int32 NextClientId = 1;
};
