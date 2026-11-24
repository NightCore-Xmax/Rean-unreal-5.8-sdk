// Copyright (c) REAN Studios. MIT License.
#include "ReanAsyncActions.h"

#include "ReanBlueprintLibrary.h"
#include "ReanConvert.h"
#include "ReanSubsystem.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"

namespace
{
FReanError MissingSubsystem()
{
	return ReanConvert::LocalError(EReanErrorCode::NotSupported,
		TEXT("No REAN subsystem: call this from a game (not a dedicated server) with a valid world context."));
}

template <typename TAction>
TAction* MakeAction(UObject* WorldContextObject)
{
	TAction* Action = NewObject<TAction>();
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}
} // namespace

UReanSubsystem* UReanAsyncActionBase::FindSubsystem() const
{
	return UReanBlueprintLibrary::GetReanSubsystem(WorldContext.Get());
}

void UReanAsyncActionBase::FailNextFrame(TFunction<void()> Fn)
{
	TWeakObjectPtr<UReanAsyncActionBase> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, Fn]() {
		if (WeakThis.IsValid() && Fn) Fn();
	});
}

// ─── Sign in ────────────────────────────────────────────────────────────────

UReanSignInWithLauncherAction* UReanSignInWithLauncherAction::SignInWithLauncher(UObject* WorldContextObject)
{
	UReanSignInWithLauncherAction* Action = MakeAction<UReanSignInWithLauncherAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanSignInWithLauncherAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FReanUser(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->SignInWithLauncher([WeakThis](const FReanError& Error, const FReanUser& User) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(User, Error);
		else Self->OnFailure.Broadcast(User, Error);
		Self->SetReadyToDestroy();
	});
}

UReanSignInWithDeviceAction* UReanSignInWithDeviceAction::SignInWithDevice(UObject* WorldContextObject)
{
	UReanSignInWithDeviceAction* Action = MakeAction<UReanSignInWithDeviceAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanSignInWithDeviceAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FReanUser(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->SignInWithDevice(
		[WeakThis](const FReanDeviceCode& Code) {
			ThisClass* Self = WeakThis.Get();
			if (!Self) return;
			Self->QrTexture = UReanBlueprintLibrary::MakeQrCodeTexture(Code.VerificationUriComplete);
			Self->OnCode.Broadcast(Code, Self->QrTexture);
		},
		[WeakThis](const FReanError& Error, const FReanUser& User) {
			ThisClass* Self = WeakThis.Get();
			if (!Self) return;
			if (Error.IsOk()) Self->OnSuccess.Broadcast(User, Error);
			else Self->OnFailure.Broadcast(User, Error);
			Self->QrTexture = nullptr;
			Self->SetReadyToDestroy();
		});
}

UReanRestoreSessionAction* UReanRestoreSessionAction::RestoreSession(UObject* WorldContextObject)
{
	UReanRestoreSessionAction* Action = MakeAction<UReanRestoreSessionAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanRestoreSessionAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FReanUser(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->RestoreSession([WeakThis](const FReanError& Error, const FReanUser& User) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(User, Error);
		else Self->OnFailure.Broadcast(User, Error);
		Self->SetReadyToDestroy();
	});
}

UReanSignInWithBrowserAction* UReanSignInWithBrowserAction::SignInWithBrowser(UObject* WorldContextObject)
{
	UReanSignInWithBrowserAction* Action = MakeAction<UReanSignInWithBrowserAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanSignInWithBrowserAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	FReanError StartError = MissingSubsystem();
	const bool bStarted = Rean && Rean->SignInWithBrowser(
		[WeakThis](const FReanError& Error, const FReanUser& User) {
			ThisClass* Self = WeakThis.Get();
			if (!Self) return;
			if (Error.IsOk()) Self->OnSuccess.Broadcast(User, Error);
			else Self->OnFailure.Broadcast(User, Error);
			Self->SetReadyToDestroy();
		},
		StartError);
	if (!bStarted)
	{
		FailNextFrame([WeakThis, StartError]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FReanUser(), StartError);
			Self->SetReadyToDestroy();
		});
	}
}

// ─── Social ─────────────────────────────────────────────────────────────────

UReanGetFriendsAction* UReanGetFriendsAction::GetFriends(UObject* WorldContextObject)
{
	UReanGetFriendsAction* Action = MakeAction<UReanGetFriendsAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanGetFriendsAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(TArray<FReanFriend>(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->GetFriends([WeakThis](const FReanError& Error, const TArray<FReanFriend>& Friends) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(Friends, Error);
		else Self->OnFailure.Broadcast(Friends, Error);
		Self->SetReadyToDestroy();
	});
}

UReanGetPresenceAction* UReanGetPresenceAction::GetPresence(UObject* WorldContextObject, const TArray<FString>& PlayerIds)
{
	UReanGetPresenceAction* Action = MakeAction<UReanGetPresenceAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Ids = PlayerIds;
	return Action;
}

void UReanGetPresenceAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(TArray<FReanPlayerPresence>(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->GetPresence(Ids, [WeakThis](const FReanError& Error, const TArray<FReanPlayerPresence>& Presence) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(Presence, Error);
		else Self->OnFailure.Broadcast(Presence, Error);
		Self->SetReadyToDestroy();
	});
}

UReanGetInvitesAction* UReanGetInvitesAction::GetInvites(UObject* WorldContextObject)
{
	UReanGetInvitesAction* Action = MakeAction<UReanGetInvitesAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	return Action;
}

void UReanGetInvitesAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(TArray<FReanInvite>(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->GetInvites([WeakThis](const FReanError& Error, const TArray<FReanInvite>& Invites) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(Invites, Error);
		else Self->OnFailure.Broadcast(Invites, Error);
		Self->SetReadyToDestroy();
	});
}

UReanSendInviteAction* UReanSendInviteAction::SendInvite(UObject* WorldContextObject, const FString& FriendId, const FString& JoinSecret, const FString& Message)
{
	UReanSendInviteAction* Action = MakeAction<UReanSendInviteAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->To = FriendId;
	Action->Secret = JoinSecret;
	Action->Text = Message;
	return Action;
}

void UReanSendInviteAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FReanSentInvite(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->SendInvite(To, Secret, Text, [WeakThis](const FReanError& Error, const FReanSentInvite& Invite) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(Invite, Error);
		else Self->OnFailure.Broadcast(Invite, Error);
		Self->SetReadyToDestroy();
	});
}

UReanRespondToInviteAction* UReanRespondToInviteAction::RespondToInvite(UObject* WorldContextObject, const FString& InviteId, bool bAccept)
{
	UReanRespondToInviteAction* Action = MakeAction<UReanRespondToInviteAction>(WorldContextObject);
	Action->WorldContext = WorldContextObject;
	Action->Id = InviteId;
	Action->bAcceptInvite = bAccept;
	return Action;
}

void UReanRespondToInviteAction::Activate()
{
	TWeakObjectPtr<ThisClass> WeakThis(this);
	UReanSubsystem* Rean = FindSubsystem();
	if (!Rean)
	{
		return FailNextFrame([WeakThis]() {
			ThisClass* Self = WeakThis.Get();
			Self->OnFailure.Broadcast(FString(), MissingSubsystem());
			Self->SetReadyToDestroy();
		});
	}
	Rean->RespondToInvite(Id, bAcceptInvite, [WeakThis](const FReanError& Error, const FString& JoinSecret) {
		ThisClass* Self = WeakThis.Get();
		if (!Self) return;
		if (Error.IsOk()) Self->OnSuccess.Broadcast(JoinSecret, Error);
		else Self->OnFailure.Broadcast(JoinSecret, Error);
		Self->SetReadyToDestroy();
	});
}
