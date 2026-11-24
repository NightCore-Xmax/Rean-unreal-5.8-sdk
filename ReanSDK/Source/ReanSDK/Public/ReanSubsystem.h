// Copyright (c) REAN Studios. MIT License.
//
// UReanSubsystem: Sign in with REAN and the REAN Social API for one game instance.
//
//   UReanSubsystem* Rean = GetGameInstance()->GetSubsystem<UReanSubsystem>();
//
// C++: the functions below take TFunction callbacks. Every callback runs later
// on the game thread (never inside the call), exactly once. Capture weak
// pointers (TWeakObjectPtr) to UObjects in callbacks.
// Blueprints: use the async nodes in ReanAsyncActions.h (Sign In With Device,
// Get Friends, ...) and the events below.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ReanTypes.h"
#include "ReanSubsystem.generated.h"

class IReanTokenStore;
class FReanLoopbackListener;
namespace rean
{
class Client;
struct Event;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReanInviteReceivedSignature, const FReanInvite&, Invite);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReanInviteAnsweredSignature, const FReanInviteAnswer&, Answer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReanEventSignature, const FReanEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanSessionChangedSignature, EReanSessionChange, Change, const FReanError&, Reason);

UCLASS()
class REANSDK_API UReanSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	using FSignInDone = TFunction<void(const FReanError& Error, const FReanUser& User)>;

	// ── Sign in ─────────────────────────────────────────────────────────────

	/** True when the REAN Launcher started the game with an unused -ReanAuthCode=rlc_… */
	UFUNCTION(BlueprintPure, Category = "REAN|Sign In")
	bool WasStartedByLauncher() const;

	/** Signs in with the launcher's one-time code from the command line (5 minutes, single use). */
	void SignInWithLauncher(FSignInDone Done);
	/** Same, with a code you got another way. */
	void SignInWithLauncherCode(const FString& LauncherCode, FSignInDone Done);
	/** Device sign-in: OnCode gets the code to show; the player enters it at reanstudios.com/activate. */
	void SignInWithDevice(TFunction<void(const FReanDeviceCode& Code)> OnCode, FSignInDone Done);
	/** Signs back in with the saved session. Fails with NoSavedSession when there is none. */
	void RestoreSession(FSignInDone Done);
	/** Signs in with a refresh token you stored yourself. */
	void SignInWithRefreshToken(const FString& RefreshToken, FSignInDone Done);
	/**
	 * Desktop only: opens reanstudios.com in the system browser (PKCE, loopback
	 * redirect on 127.0.0.1). Returns false (and never calls Done) when it could not start.
	 */
	bool SignInWithBrowser(FSignInDone Done, FReanError& OutError);

	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In")
	void CancelSignIn();

	/** Clears presence, revokes the session on REAN and forgets the saved session. */
	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In")
	void SignOut();

	UFUNCTION(BlueprintPure, Category = "REAN|Sign In")
	bool IsSignedIn() const;

	UFUNCTION(BlueprintPure, Category = "REAN|Sign In")
	bool IsSigningIn() const;

	/** The signed-in player (empty when signed out). */
	UFUNCTION(BlueprintPure, Category = "REAN|Sign In")
	FReanUser GetCurrentUser() const;

	/** Whether REAN granted a scope, e.g. "presence.write". */
	UFUNCTION(BlueprintPure, Category = "REAN|Sign In")
	bool HasScope(const FString& Scope) const;

	/** Fires on sign-in, sign-out and when REAN ends the session. */
	UPROPERTY(BlueprintAssignable, Category = "REAN|Sign In")
	FReanSessionChangedSignature OnSessionChanged;

	// ── Social API ──────────────────────────────────────────────────────────

	void GetMe(TFunction<void(const FReanError& Error, const FReanUser& User)> Done);
	void GetFriends(TFunction<void(const FReanError& Error, const TArray<FReanFriend>& Friends)> Done);
	void GetPresence(const TArray<FString>& PlayerIds, TFunction<void(const FReanError& Error, const TArray<FReanPlayerPresence>& Presence)> Done);
	void GetInvites(TFunction<void(const FReanError& Error, const TArray<FReanInvite>& Invites)> Done);
	void SendInvite(const FString& FriendId, const FString& JoinSecret, const FString& Message,
		TFunction<void(const FReanError& Error, const FReanSentInvite& Invite)> Done);
	void RespondToInvite(const FString& InviteId, bool bAccept, TFunction<void(const FReanError& Error, const FString& JoinSecret)> Done);

	/**
	 * Shows what the player is doing to friends and keeps it alive (re-sent every
	 * 60 s; REAN forgets presence after 120 s) until Clear Activity or Sign Out.
	 * Returns false with an error for invalid values or when not signed in.
	 */
	UFUNCTION(BlueprintCallable, Category = "REAN|Presence", meta = (ReturnDisplayName = "Valid"))
	bool SetActivity(const FReanActivity& Activity, FReanError& Error);

	UFUNCTION(BlueprintCallable, Category = "REAN|Presence")
	void ClearActivity();

	UFUNCTION(BlueprintPure, Category = "REAN|Presence")
	bool HasActivity() const;

	/** A friend invited the player (polled every EventPollSeconds while bound). */
	UPROPERTY(BlueprintAssignable, Category = "REAN|Invites")
	FReanInviteReceivedSignature OnInviteReceived;

	/** A friend accepted or declined an invite the player sent. */
	UPROPERTY(BlueprintAssignable, Category = "REAN|Invites")
	FReanInviteAnsweredSignature OnInviteAnswered;

	/** Every event, including types newer than this SDK. */
	UPROPERTY(BlueprintAssignable, Category = "REAN|Invites")
	FReanEventSignature OnEvent;

	// ── Setup ───────────────────────────────────────────────────────────────

	/**
	 * Replaces where the session is stored (default: DPAPI on Windows, SaveGame
	 * elsewhere). Call before the first sign-in, e.g. from your GameInstance's Init.
	 */
	void SetTokenStore(TSharedRef<IReanTokenStore> Store);

	/** The engine-independent client, for advanced use. Valid while the subsystem lives. */
	rean::Client* GetCoreClient() const { return Client.Get(); }

	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void CreateClient();
	void UpdateEventListening();
	void HandleEvent(const rean::Event& Event);
	void TickLoopback();
	void CloseLoopback();
	FString LauncherCodeFromCommandLine() const;

	TSharedPtr<rean::Client> Client;
	TSharedPtr<IReanTokenStore> TokenStore;
	TSharedPtr<FReanLoopbackListener> Loopback;
	bool bInitialized = false;
	bool bListeningForEvents = false;
	bool bLauncherCodeUsed = false;
};
