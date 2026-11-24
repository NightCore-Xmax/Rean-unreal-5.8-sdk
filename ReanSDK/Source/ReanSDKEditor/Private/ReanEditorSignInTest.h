// Copyright (c) REAN Studios. MIT License.
//
// "Test sign-in" in the editor: runs a device-code sign-in with the client id
// from Project Settings, without playing the game, and prints the player.
// Nothing is saved; the test session is revoked at the end.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class SNotificationItem;
namespace rean
{
class Client;
}

class FReanEditorSignInTest : public TSharedFromThis<FReanEditorSignInTest>
{
public:
	~FReanEditorSignInTest();
	void Start();
	void Cancel();
	bool IsRunning() const { return Client.IsValid(); }

private:
	bool Tick(float DeltaTime);
	void Finish(bool bSuccess, const FString& Message);
	void SetNotification(const FText& Text, bool bPending, bool bSuccess, const FString& OpenUrl);

	TSharedPtr<rean::Client> Client;
	FTSTicker::FDelegateHandle TickerHandle;
	TWeakPtr<SNotificationItem> Notification;
	double StopAt = 0.0;
};
