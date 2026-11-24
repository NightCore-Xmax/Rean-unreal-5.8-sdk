// Copyright (c) REAN Studios. MIT License.
//
// Blueprint types of the REAN SDK. They mirror rean:: types of ReanCore.
#pragma once

#include "CoreMinimal.h"
#include "ReanTypes.generated.h"

/** What went wrong. None = success. */
UENUM(BlueprintType)
enum class EReanErrorCode : uint8
{
	None,
	/** A parameter is empty, too long or malformed. Nothing was sent. */
	InvalidArgument,
	/** The client id in Project Settings > Plugins > REAN SDK is missing or malformed. */
	InvalidConfig,
	NotSignedIn,
	/** Restore Session found no saved session: sign in with the device code (or the launcher). */
	NoSavedSession,
	/** A sign-in is already running, or a player is already signed in. */
	Busy,
	Cancelled,
	/** REAN could not be reached. */
	Network,
	/** Too many requests (HTTP 429). Wait and retry. */
	RateLimited,
	/** REAN does not know the client id, or the app is not active. */
	InvalidClient,
	/** The launcher / authorization code is invalid, expired or already used. */
	InvalidGrant,
	/** The session ended (expired, revoked, or the player removed your app): sign in again. */
	SessionEnded,
	/** The player declined, or REAN refused (403). */
	Denied,
	/** The sign-in code or the invite expired. */
	Expired,
	/** Your app did not ask for the scope this needs. */
	MissingScope,
	/** Invites only go to REAN friends. */
	NotFriends,
	NotFound,
	/** REAN rejected the request (400). */
	InvalidRequest,
	/** REAN failed or answered something unexpected. */
	Server,
	/** Not available on this platform. */
	NotSupported,
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanError
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	EReanErrorCode Code = EReanErrorCode::None;

	/** HTTP status of REAN's answer (0 when there was none). */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int32 HttpStatus = 0;

	/** Machine code from REAN, e.g. not_friends, invalid_grant (may be empty). */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString ApiCode;

	/** Human-readable explanation (English). */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Message;

	bool IsOk() const { return Code == EReanErrorCode::None; }
};

/** A REAN player. Id is permanent (the OpenID sub): use it as the key, not the username. */
USTRUCT(BlueprintType)
struct REANSDK_API FReanUser
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Username;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString DisplayName;

	/** May be empty. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString AvatarUrl;

	/** Only with the email scope. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Email;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bEmailVerified = false;
};

UENUM(BlueprintType)
enum class EReanPresenceStatus : uint8
{
	Offline,
	Online,
	Away,
	DoNotDisturb,
	InGame,
	/** A status this SDK version does not know (see StatusText). */
	Unknown,
};

/** What a friend is doing. Party and join information only appear when the friend is in YOUR game. */
USTRUCT(BlueprintType)
struct REANSDK_API FReanFriendActivity
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString AppClientId;

	/** The game the friend is in. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString AppName;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString AppIconUrl;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Details;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString State;

	/** Unix seconds, 0 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int64 StartedAt = 0;

	/** The friend is in your game: party and joinable are meaningful. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bInYourGame = false;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bHasParty = false;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString PartyId;

	/** -1 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int32 PartySize = -1;

	/** -1 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int32 PartyMax = -1;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bJoinable = false;
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanPresence
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	EReanPresenceStatus Status = EReanPresenceStatus::Offline;

	/** online, away, dnd, in_game or offline. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString StatusText = TEXT("offline");

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bHasActivity = false;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanFriendActivity Activity;
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanFriend
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanUser User;

	/** ISO 8601, may be empty. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString FriendsSince;

	/** False when your app did not ask for presence.read. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bHasPresence = false;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanPresence Presence;
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanPlayerPresence
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanPresence Presence;
};

UENUM(BlueprintType)
enum class EReanActivityStatus : uint8
{
	InGame,
	Online,
	Away,
	DoNotDisturb,
};

/** Rich presence for the signed-in player, shown to friends. */
USTRUCT(BlueprintType)
struct REANSDK_API FReanActivity
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	EReanActivityStatus Status = EReanActivityStatus::InGame;

	/** What the player is doing, e.g. "Ranked". At most 128 characters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	FString Details;

	/** More detail, e.g. "Round 3". At most 128 characters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	FString State;

	/** Your lobby / party id (at most 128). Only friends in your game see it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	FString PartyId;

	/** 0..1000, -1 = not set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN", meta = (ClampMin = "-1", ClampMax = "1000"))
	int32 PartySize = -1;

	/** 0..1000, -1 = not set. PartySize must not be larger. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN", meta = (ClampMin = "-1", ClampMax = "1000"))
	int32 PartyMax = -1;

	/** Friends in your game may ask to join. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	bool bJoinable = false;

	/** When the match started, in unix SECONDS (0 = not set). FDateTime::UtcNow().ToUnixTimestamp(). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "REAN")
	int64 StartedAt = 0;
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanInvite
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Id;

	/** The friend who invited the player. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanUser From;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Message;

	/** What your game needs to join (given to the player when they accept). */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString JoinSecret;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString CreatedAt;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString ExpiresAt;
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanSentInvite
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString To;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString ExpiresAt;
};

/** A friend accepted or declined an invite the player sent. */
USTRUCT(BlueprintType)
struct REANSDK_API FReanInviteAnswer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString InviteId;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanUser By;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	bool bAccepted = false;
};

UENUM(BlueprintType)
enum class EReanEventType : uint8
{
	InviteReceived,
	InviteAccepted,
	InviteDeclined,
	/** A newer event type: see TypeName and RawData. */
	Unknown,
};

USTRUCT(BlueprintType)
struct REANSDK_API FReanEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	EReanEventType Type = EReanEventType::Unknown;

	/** invite.received, invite.accepted, invite.declined, ... */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString TypeName;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString CreatedAt;

	/** InviteReceived only. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanInvite Invite;

	/** InviteAccepted / InviteDeclined only. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FReanInviteAnswer Answer;

	/** The event's data as JSON. */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString RawData;
};

/** Device sign-in: show UserCode and VerificationUri; VerificationUriComplete is for a QR code. */
USTRUCT(BlueprintType)
struct REANSDK_API FReanDeviceCode
{
	GENERATED_BODY()

	/** e.g. BCDF-GHJK */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString UserCode;

	/** https://reanstudios.com/activate */
	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString VerificationUri;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	FString VerificationUriComplete;

	UPROPERTY(BlueprintReadOnly, Category = "REAN")
	int32 ExpiresInSeconds = 600;
};

UENUM(BlueprintType)
enum class EReanSessionChange : uint8
{
	SignedIn,
	/** Sign Out was called. */
	SignedOut,
	/** REAN ended the session (expired, revoked, the player removed your app). Sign in again. */
	SessionEnded,
};
