// Copyright (c) REAN Studios. MIT License.
//
// Blueprint async nodes. Each has OnSuccess / OnFailure pins (exactly one fires,
// on a later frame) carrying the result and an FReanError.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "ReanTypes.h"
#include "ReanAsyncActions.generated.h"

class UReanSubsystem;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanSignInPin, const FReanUser&, User, const FReanError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanDeviceCodePin, const FReanDeviceCode&, Code, UTexture2D*, QrCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanFriendsPin, const TArray<FReanFriend>&, Friends, const FReanError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanPresencePin, const TArray<FReanPlayerPresence>&, Presence, const FReanError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanInvitesPin, const TArray<FReanInvite>&, Invites, const FReanError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanSentInvitePin, const FReanSentInvite&, Invite, const FReanError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FReanJoinPin, const FString&, JoinSecret, const FReanError&, Error);

/** Shared plumbing: finds the subsystem, fails cleanly when there is none. */
UCLASS(Abstract)
class REANSDK_API UReanAsyncActionBase : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

protected:
	UReanSubsystem* FindSubsystem() const;
	/** Runs Fn on the next frame (used to report "no subsystem" without firing pins inside Activate). */
	void FailNextFrame(TFunction<void()> Fn);

	/** Weak: the node must not keep a level object alive. */
	TWeakObjectPtr<UObject> WorldContext;
};

/** Signs in with the -ReanAuthCode the REAN Launcher passed to the game. */
UCLASS()
class REANSDK_API UReanSignInWithLauncherAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Sign In With Launcher"))
	static UReanSignInWithLauncherAction* SignInWithLauncher(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnFailure;

	virtual void Activate() override;
};

/** Device sign-in: On Code gives the code (and a QR code) to show; the player enters it at reanstudios.com/activate. */
UCLASS()
class REANSDK_API UReanSignInWithDeviceAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Sign In With Device"))
	static UReanSignInWithDeviceAction* SignInWithDevice(UObject* WorldContextObject);

	/** Show Code.UserCode and Code.VerificationUri (and the QR code texture). */
	UPROPERTY(BlueprintAssignable)
	FReanDeviceCodePin OnCode;

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnSuccess;

	/** Denied (the player declined), Expired (offer to start again), Cancelled, Network, ... */
	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnFailure;

	virtual void Activate() override;

private:
	/** Kept alive while the code is shown. */
	UPROPERTY()
	TObjectPtr<UTexture2D> QrTexture;
};

/** Signs back in with the saved session. OnFailure with NoSavedSession: show device sign-in. */
UCLASS()
class REANSDK_API UReanRestoreSessionAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Restore Session"))
	static UReanRestoreSessionAction* RestoreSession(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnFailure;

	virtual void Activate() override;
};

/** Desktop: signs in through reanstudios.com in the system browser. */
UCLASS()
class REANSDK_API UReanSignInWithBrowserAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Sign In", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Sign In With Browser"))
	static UReanSignInWithBrowserAction* SignInWithBrowser(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanSignInPin OnFailure;

	virtual void Activate() override;
};

/** The player's REAN friends, with presence when the app has presence.read. */
UCLASS()
class REANSDK_API UReanGetFriendsAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Friends", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Friends"))
	static UReanGetFriendsAction* GetFriends(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable)
	FReanFriendsPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanFriendsPin OnFailure;

	virtual void Activate() override;
};

/** Presence of up to 100 players (non-friends read as offline). */
UCLASS()
class REANSDK_API UReanGetPresenceAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Presence", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Presence"))
	static UReanGetPresenceAction* GetPresence(UObject* WorldContextObject, const TArray<FString>& PlayerIds);

	UPROPERTY(BlueprintAssignable)
	FReanPresencePin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanPresencePin OnFailure;

	virtual void Activate() override;

private:
	TArray<FString> Ids;
};

/** Pending invites the player received in your game. */
UCLASS()
class REANSDK_API UReanGetInvitesAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Invites", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Invites"))
	static UReanGetInvitesAction* GetInvites(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable)
	FReanInvitesPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanInvitesPin OnFailure;

	virtual void Activate() override;
};

/** Invites a friend. JoinSecret (<= 256) is what your game needs to join: lobby id, server address and ticket... */
UCLASS()
class REANSDK_API UReanSendInviteAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Invites", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Send Invite"))
	static UReanSendInviteAction* SendInvite(UObject* WorldContextObject, const FString& FriendId, const FString& JoinSecret, const FString& Message);

	UPROPERTY(BlueprintAssignable)
	FReanSentInvitePin OnSuccess;

	/** NotFriends, RateLimited (30 per 10 minutes), MissingScope, ... */
	UPROPERTY(BlueprintAssignable)
	FReanSentInvitePin OnFailure;

	virtual void Activate() override;

private:
	FString To;
	FString Secret;
	FString Text;
};

/** Accepts or declines an invite. Accepting returns the inviter's join secret. Expired after 10 minutes. */
UCLASS()
class REANSDK_API UReanRespondToInviteAction : public UReanAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "REAN|Invites", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Respond To Invite"))
	static UReanRespondToInviteAction* RespondToInvite(UObject* WorldContextObject, const FString& InviteId, bool bAccept);

	UPROPERTY(BlueprintAssignable)
	FReanJoinPin OnSuccess;

	UPROPERTY(BlueprintAssignable)
	FReanJoinPin OnFailure;

	virtual void Activate() override;

private:
	FString Id;
	bool bAcceptInvite = false;
};
