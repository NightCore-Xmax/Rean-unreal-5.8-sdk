// Copyright (c) REAN Studios. MIT License.
#include "ReanLoopbackListener.h"

#include "ReanLog.h"
#include "Rean/ReanClient.h"

#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
constexpr size_t MaxHeadBytes = 16 * 1024;
constexpr double ConnectionTimeoutSeconds = 10.0;

ISocketSubsystem* Sockets() { return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); }
} // namespace

FReanLoopbackListener::~FReanLoopbackListener()
{
	Close();
}

bool FReanLoopbackListener::Open(int32& OutPort)
{
	Close();
	ISocketSubsystem* Subsystem = Sockets();
	if (!Subsystem) return false;
	TSharedRef<FInternetAddr> Address = Subsystem->CreateInternetAddr();
	Address->SetLoopbackAddress();
	Address->SetPort(0);  // the OS picks a free port
	Listener = Subsystem->CreateSocket(NAME_Stream, TEXT("REAN sign-in redirect"), Address->GetProtocolType());
	if (!Listener) return false;
	if (!Listener->Bind(*Address) || !Listener->Listen(4) || !Listener->SetNonBlocking(true))
	{
		UE_LOG(LogRean, Warning, TEXT("Could not listen on 127.0.0.1 for the browser sign-in redirect."));
		Close();
		return false;
	}
	OutPort = Listener->GetPortNo();
	return OutPort > 0;
}

bool FReanLoopbackListener::Poll(FString& OutTarget)
{
	if (!Listener) return false;
	if (!Connection)
	{
		bool bPending = false;
		if (!Listener->HasPendingConnection(bPending) || !bPending) return false;
		Connection = Listener->Accept(TEXT("REAN sign-in redirect connection"));
		if (!Connection) return false;
		Connection->SetNonBlocking(true);
		Head.clear();
		ConnectionStarted = FPlatformTime::Seconds();
	}
	uint8 Chunk[2048];
	int32 Read = 0;
	while (Head.size() < MaxHeadBytes && Connection->Recv(Chunk, static_cast<int32>(sizeof(Chunk)), Read) && Read > 0)
	{
		Head.append(reinterpret_cast<const char*>(Chunk), static_cast<size_t>(Read));
	}
	if (Head.find("\r\n\r\n") != std::string::npos)
	{
		std::string Method, Target;
		if (rean::ParseHttpRequestTarget(Head, Method, Target))
		{
			OutTarget = FString(UTF8_TO_TCHAR(Target.c_str()));
			return true;
		}
		Respond("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
		return false;
	}
	if (Head.size() >= MaxHeadBytes || FPlatformTime::Seconds() - ConnectionStarted > ConnectionTimeoutSeconds) DropConnection();
	return false;
}

void FReanLoopbackListener::Respond(const std::string& RawResponse)
{
	if (!Connection) return;
	size_t Offset = 0;
	const double Deadline = FPlatformTime::Seconds() + 2.0;
	while (Offset < RawResponse.size() && FPlatformTime::Seconds() < Deadline)
	{
		int32 Sent = 0;
		const int32 Count = static_cast<int32>(RawResponse.size() - Offset);
		if (!Connection->Send(reinterpret_cast<const uint8*>(RawResponse.data() + Offset), Count, Sent)) break;
		Offset += static_cast<size_t>(Sent > 0 ? Sent : 0);
	}
	DropConnection();
}

void FReanLoopbackListener::DropConnection()
{
	if (!Connection) return;
	Connection->Close();
	if (ISocketSubsystem* Subsystem = Sockets()) Subsystem->DestroySocket(Connection);
	Connection = nullptr;
	Head.clear();
}

void FReanLoopbackListener::Close()
{
	DropConnection();
	if (!Listener) return;
	Listener->Close();
	if (ISocketSubsystem* Subsystem = Sockets()) Subsystem->DestroySocket(Listener);
	Listener = nullptr;
}
