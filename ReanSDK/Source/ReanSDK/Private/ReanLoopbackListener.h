// Copyright (c) REAN Studios. MIT License.
//
// Receives the browser's redirect for browser sign-in (RFC 8252 loopback
// redirect): a tiny non-blocking HTTP listener on 127.0.0.1 and a port the OS
// picks. Bound to the loopback address only, so it is not reachable from the
// network and does not trigger a firewall prompt. Polled from the game thread.
#pragma once

#include "CoreMinimal.h"

#include <string>

class FSocket;

class FReanLoopbackListener
{
public:
	~FReanLoopbackListener();

	/** Opens the listener. OutPort is the port the OS chose. */
	bool Open(int32& OutPort);
	/** Accepts / reads without blocking. True when a whole request head arrived; OutTarget is "/callback?code=…". */
	bool Poll(FString& OutTarget);
	/** Answers the current connection (a complete HTTP response) and closes it. */
	void Respond(const std::string& RawResponse);
	void Close();
	bool IsOpen() const { return Listener != nullptr; }

private:
	void DropConnection();

	FSocket* Listener = nullptr;
	FSocket* Connection = nullptr;
	std::string Head;
	double ConnectionStarted = 0.0;
};
