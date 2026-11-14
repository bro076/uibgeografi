#include "PhoneControllerSubsystem.h"

#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogDronePhone, Log, All);

struct FPhoneClient
{
	FSocket* Socket = nullptr;
	int32 Id = 0;
	TArray<uint8> In;
	TArray<uint8> Out;
	bool bWebSocket = false;
	bool bCloseAfterFlush = false;
	bool bClosed = false;
	double LastReceive = 0.0;
	double LastSticks = -1000.0;
	FDroneSticks Sticks;
};

namespace
{
	const double HttpTimeoutSeconds = 10.0;
	const double WebSocketTimeoutSeconds = 5.0;
	const double StickTimeoutSeconds = 0.5;   // older than this = signal lost, sticks read as centred
	const int32 MaxBufferedBytes = 64 * 1024;

	FString BytesToString(const uint8* Data, int32 Length)
	{
		TArray<uint8> Terminated;
		Terminated.Append(Data, Length);
		Terminated.Add(0);
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Terminated.GetData())));
	}

	float ParseAxis(const FString& Text)
	{
		const float Value = FCString::Atof(*Text);
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.f, 1.f) : 0.f;
	}
}

void UPhoneControllerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	int32 ConfigPort = 8080;
	if (GConfig)
	{
		GConfig->GetInt(TEXT("DroneSim"), TEXT("PhonePort"), ConfigPort, GGameIni);
	}

	// Try a few ports in case the configured one is taken (e.g. two editor instances).
	for (int32 Attempt = 0; Attempt < 5 && ListenSocket == nullptr; ++Attempt)
	{
		Port = ConfigPort + Attempt;
		ListenSocket = FTcpSocketBuilder(TEXT("DroneSimPhoneServer"))
			.AsReusable()
			.AsNonBlocking()
			.BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, Port))
			.Listening(8)
			.Build();
	}

	if (ListenSocket)
	{
		ControllerUrl = FString::Printf(TEXT("http://%s:%d"), *FindLocalAddress(), Port);
		UE_LOG(LogDronePhone, Log, TEXT("Phone controller available at %s"), *ControllerUrl);
	}
	else
	{
		UE_LOG(LogDronePhone, Warning, TEXT("Could not open a TCP port for the phone controller (tried %d-%d)."), ConfigPort, ConfigPort + 4);
	}

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPhoneControllerSubsystem::OnTick));
}

void UPhoneControllerSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);

	for (const TSharedPtr<FPhoneClient>& Client : Clients)
	{
		CloseClient(*Client);
	}
	Clients.Empty();

	if (ListenSocket)
	{
		ListenSocket->Close();
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Sockets->DestroySocket(ListenSocket);
		}
		ListenSocket = nullptr;
	}

	Super::Deinitialize();
}

FString UPhoneControllerSubsystem::FindLocalAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return TEXT("127.0.0.1");
	}

	// Prefer a private LAN IPv4 address, that is what phones on the same Wi-Fi can reach.
	TArray<TSharedPtr<FInternetAddr>> Addresses;
	Sockets->GetLocalAdapterAddresses(Addresses);
	FString Fallback;
	for (const TSharedPtr<FInternetAddr>& Address : Addresses)
	{
		if (!Address.IsValid())
		{
			continue;
		}
		const FString Text = Address->ToString(false);
		if (Text.Contains(TEXT(":")) || Text.StartsWith(TEXT("127.")) || Text.StartsWith(TEXT("169.254.")))
		{
			continue;   // IPv6, loopback, link-local
		}
		if (Text.StartsWith(TEXT("192.168.")) || Text.StartsWith(TEXT("10.")) || Text.StartsWith(TEXT("172.")))
		{
			return Text;
		}
		if (Fallback.IsEmpty())
		{
			Fallback = Text;
		}
	}
	if (!Fallback.IsEmpty())
	{
		return Fallback;
	}

	bool bCanBindAll = false;
	const TSharedRef<FInternetAddr> HostAddress = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return HostAddress->ToString(false);
}

int32 UPhoneControllerSubsystem::GetNumPhones() const
{
	int32 Count = 0;
	for (const TSharedPtr<FPhoneClient>& Client : Clients)
	{
		if (Client->bWebSocket && !Client->bClosed)
		{
			++Count;
		}
	}
	return Count;
}

FPhoneClient* UPhoneControllerSubsystem::FindPilot() const
{
	for (const TSharedPtr<FPhoneClient>& Client : Clients)
	{
		if (Client->Id == PilotId && Client->bWebSocket && !Client->bClosed)
		{
			return Client.Get();
		}
	}
	return nullptr;
}

bool UPhoneControllerSubsystem::GetPilotSticks(FDroneSticks& OutSticks) const
{
	const FPhoneClient* Pilot = FindPilot();
	if (!Pilot || FPlatformTime::Seconds() - Pilot->LastSticks > StickTimeoutSeconds)
	{
		return false;
	}
	OutSticks = Pilot->Sticks;
	return true;
}

void UPhoneControllerSubsystem::ConsumeCommands(TArray<FString>& OutCommands)
{
	OutCommands = MoveTemp(PendingCommands);
	PendingCommands.Reset();
}

void UPhoneControllerSubsystem::BroadcastTelemetry(const FString& Fields)
{
	for (const TSharedPtr<FPhoneClient>& Client : Clients)
	{
		if (Client->bWebSocket && !Client->bClosed)
		{
			const TCHAR* Role = Client->Id == PilotId ? TEXT("P") : TEXT("S");
			QueueText(*Client, FString::Printf(TEXT("t|%s|%s"), Role, *Fields));
		}
	}
}

bool UPhoneControllerSubsystem::OnTick(float DeltaTime)
{
	AcceptConnections();

	for (const TSharedPtr<FPhoneClient>& Client : Clients)
	{
		PumpClient(*Client);
	}

	Clients.RemoveAll([](const TSharedPtr<FPhoneClient>& Client) { return Client->bClosed; });

	// Hand the pilot seat to the longest-connected phone if the pilot left.
	if (FindPilot() == nullptr)
	{
		PilotId = 0;
		for (const TSharedPtr<FPhoneClient>& Client : Clients)
		{
			if (Client->bWebSocket)
			{
				PilotId = Client->Id;
				break;
			}
		}
	}

	return true;   // keep ticking
}

void UPhoneControllerSubsystem::AcceptConnections()
{
	if (!ListenSocket)
	{
		return;
	}

	bool bPending = false;
	while (ListenSocket->HasPendingConnection(bPending) && bPending)
	{
		FSocket* Socket = ListenSocket->Accept(TEXT("DroneSimPhoneClient"));
		if (!Socket)
		{
			break;
		}
		Socket->SetNonBlocking(true);
		Socket->SetNoDelay(true);

		TSharedPtr<FPhoneClient> Client = MakeShared<FPhoneClient>();
		Client->Socket = Socket;
		Client->Id = NextClientId++;
		Client->LastReceive = FPlatformTime::Seconds();
		Clients.Add(Client);
	}
}

void UPhoneControllerSubsystem::PumpClient(FPhoneClient& Client)
{
	if (Client.bClosed)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();

	uint32 PendingSize = 0;
	while (Client.Socket->HasPendingData(PendingSize) && PendingSize > 0)
	{
		uint8 Buffer[4096];
		int32 BytesRead = 0;
		if (!Client.Socket->Recv(Buffer, sizeof(Buffer), BytesRead) || BytesRead <= 0)
		{
			break;
		}
		Client.In.Append(Buffer, BytesRead);
		Client.LastReceive = Now;
		if (Client.In.Num() > MaxBufferedBytes)
		{
			CloseClient(Client);
			return;
		}
	}

	if (Client.bWebSocket)
	{
		HandleFrames(Client);
	}
	else if (!Client.bCloseAfterFlush)
	{
		HandleHttp(Client);
	}
	if (Client.bClosed)
	{
		return;
	}

	Flush(Client);

	const double Timeout = Client.bWebSocket ? WebSocketTimeoutSeconds : HttpTimeoutSeconds;
	const bool bDone = Client.bCloseAfterFlush && Client.Out.Num() == 0;
	const bool bDropped = Client.Socket->GetConnectionState() == SCS_ConnectionError;
	if (bDone || bDropped || Now - Client.LastReceive > Timeout || Client.Out.Num() > 4 * MaxBufferedBytes)
	{
		CloseClient(Client);
	}
}

void UPhoneControllerSubsystem::HandleHttp(FPhoneClient& Client)
{
	// Wait for the end of the request headers.
	int32 HeaderEnd = INDEX_NONE;
	for (int32 i = 0; i + 3 < Client.In.Num(); ++i)
	{
		if (Client.In[i] == '\r' && Client.In[i + 1] == '\n' && Client.In[i + 2] == '\r' && Client.In[i + 3] == '\n')
		{
			HeaderEnd = i;
			break;
		}
	}
	if (HeaderEnd == INDEX_NONE)
	{
		return;
	}

	const FString Request = BytesToString(Client.In.GetData(), HeaderEnd);
	Client.In.RemoveAt(0, HeaderEnd + 4);

	TArray<FString> Lines;
	Request.ParseIntoArray(Lines, TEXT("\r\n"));
	if (Lines.Num() == 0)
	{
		CloseClient(Client);
		return;
	}

	TArray<FString> RequestLine;
	Lines[0].ParseIntoArray(RequestLine, TEXT(" "));
	const FString Method = RequestLine.Num() > 0 ? RequestLine[0] : FString();
	FString Path = RequestLine.Num() > 1 ? RequestLine[1] : FString();
	int32 QueryIndex = INDEX_NONE;
	if (Path.FindChar(TEXT('?'), QueryIndex))
	{
		Path.LeftInline(QueryIndex);
	}

	TMap<FString, FString> Headers;
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		FString Key, Value;
		if (Lines[i].Split(TEXT(":"), &Key, &Value))
		{
			Headers.Add(Key.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
		}
	}

	const FString* Upgrade = Headers.Find(TEXT("upgrade"));
	const FString* WebSocketKey = Headers.Find(TEXT("sec-websocket-key"));
	if (Upgrade && WebSocketKey && Upgrade->ToLower().Contains(TEXT("websocket")))
	{
		// RFC 6455 handshake: accept = base64(sha1(key + magic GUID))
		const FString AcceptSource = *WebSocketKey + TEXT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
		const FTCHARToUTF8 AcceptUtf8(*AcceptSource);
		uint8 Hash[20];
		FSHA1::HashBuffer(AcceptUtf8.Get(), AcceptUtf8.Length(), Hash);
		const FString Accept = FBase64::Encode(Hash, 20);

		QueueRaw(Client, FString::Printf(
			TEXT("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n"),
			*Accept));
		Client.bWebSocket = true;
		UE_LOG(LogDronePhone, Log, TEXT("Phone %d connected"), Client.Id);
		HandleFrames(Client);   // in case frames arrived together with the handshake
		return;
	}

	if (Method == TEXT("GET") && (Path == TEXT("/") || Path == TEXT("/index.html")))
	{
		// Loaded per request so the page can be edited while the sim is running.
		const FString PagePath = FPaths::ProjectContentDir() / TEXT("Web/controller.html");
		FString Page;
		if (!FFileHelper::LoadFileToString(Page, *PagePath))
		{
			Page = FString::Printf(TEXT("<h1>DroneSim</h1><p>Missing controller page: %s</p>"), *PagePath);
		}
		const FTCHARToUTF8 PageUtf8(*Page);
		QueueRaw(Client, FString::Printf(
			TEXT("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n"),
			PageUtf8.Length()));
		Client.Out.Append(reinterpret_cast<const uint8*>(PageUtf8.Get()), PageUtf8.Length());
	}
	else
	{
		QueueRaw(Client, TEXT("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
	}
	Client.bCloseAfterFlush = true;
}

void UPhoneControllerSubsystem::HandleFrames(FPhoneClient& Client)
{
	while (!Client.bClosed && Client.In.Num() >= 2)
	{
		const uint8 Opcode = Client.In[0] & 0x0F;
		const bool bMasked = (Client.In[1] & 0x80) != 0;
		int32 PayloadLength = Client.In[1] & 0x7F;
		int32 HeaderLength = 2;

		if (PayloadLength == 126)
		{
			if (Client.In.Num() < 4)
			{
				return;
			}
			PayloadLength = (static_cast<int32>(Client.In[2]) << 8) | static_cast<int32>(Client.In[3]);
			HeaderLength = 4;
		}
		else if (PayloadLength == 127)
		{
			CloseClient(Client);   // we never expect 64-bit length frames
			return;
		}

		const int32 MaskOffset = HeaderLength;
		if (bMasked)
		{
			HeaderLength += 4;
		}
		if (Client.In.Num() < HeaderLength + PayloadLength)
		{
			return;   // wait for the rest of the frame
		}

		TArray<uint8> Payload;
		Payload.Append(Client.In.GetData() + HeaderLength, PayloadLength);
		if (bMasked)
		{
			for (int32 i = 0; i < PayloadLength; ++i)
			{
				Payload[i] ^= Client.In[MaskOffset + (i & 3)];
			}
		}
		Client.In.RemoveAt(0, HeaderLength + PayloadLength);

		switch (Opcode)
		{
		case 0x1:   // text
			HandleText(Client, BytesToString(Payload.GetData(), Payload.Num()));
			break;
		case 0x8:   // close
			CloseClient(Client);
			break;
		case 0x9:   // ping -> pong
			QueueFrame(Client, 0xA, Payload.GetData(), Payload.Num());
			break;
		default:
			break;
		}
	}
}

void UPhoneControllerSubsystem::HandleText(FPhoneClient& Client, const FString& Message)
{
	TArray<FString> Parts;
	Message.ParseIntoArray(Parts, TEXT("|"));
	if (Parts.Num() < 2)
	{
		return;
	}

	if (Parts[0] == TEXT("s") && Parts.Num() >= 6)
	{
		Client.Sticks.LeftX = ParseAxis(Parts[1]);
		Client.Sticks.LeftY = ParseAxis(Parts[2]);
		Client.Sticks.RightX = ParseAxis(Parts[3]);
		Client.Sticks.RightY = ParseAxis(Parts[4]);
		Client.Sticks.Gimbal = ParseAxis(Parts[5]);
		Client.LastSticks = FPlatformTime::Seconds();
	}
	else if (Parts[0] == TEXT("c"))
	{
		if (Parts[1] == TEXT("claim"))
		{
			PilotId = Client.Id;
		}
		else if (Client.Id == PilotId && PendingCommands.Num() < 32)
		{
			PendingCommands.Add(Parts.Num() > 2 ? Parts[1] + TEXT("|") + Parts[2] : Parts[1]);
		}
	}
}

void UPhoneControllerSubsystem::QueueFrame(FPhoneClient& Client, uint8 Opcode, const uint8* Payload, int32 Length)
{
	if (Length > 65535)
	{
		return;
	}
	Client.Out.Add(0x80 | Opcode);   // FIN + opcode, server frames are not masked
	if (Length < 126)
	{
		Client.Out.Add(static_cast<uint8>(Length));
	}
	else
	{
		Client.Out.Add(126);
		Client.Out.Add(static_cast<uint8>((Length >> 8) & 0xFF));
		Client.Out.Add(static_cast<uint8>(Length & 0xFF));
	}
	if (Length > 0)
	{
		Client.Out.Append(Payload, Length);
	}
}

void UPhoneControllerSubsystem::QueueText(FPhoneClient& Client, const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	QueueFrame(Client, 0x1, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

void UPhoneControllerSubsystem::QueueRaw(FPhoneClient& Client, const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	Client.Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

void UPhoneControllerSubsystem::Flush(FPhoneClient& Client)
{
	while (Client.Out.Num() > 0)
	{
		int32 BytesSent = 0;
		if (!Client.Socket->Send(Client.Out.GetData(), Client.Out.Num(), BytesSent) || BytesSent <= 0)
		{
			break;   // socket buffer full, try again next tick
		}
		Client.Out.RemoveAt(0, BytesSent);
	}
}

void UPhoneControllerSubsystem::CloseClient(FPhoneClient& Client)
{
	if (Client.bClosed)
	{
		return;
	}
	Client.bClosed = true;
	if (Client.bWebSocket)
	{
		UE_LOG(LogDronePhone, Log, TEXT("Phone %d disconnected"), Client.Id);
	}
	if (Client.Socket)
	{
		Client.Socket->Close();
		if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Sockets->DestroySocket(Client.Socket);
		}
		Client.Socket = nullptr;
	}
}
