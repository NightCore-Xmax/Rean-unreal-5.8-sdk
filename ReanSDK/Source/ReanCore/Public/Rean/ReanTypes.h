// Copyright (c) REAN Studios. MIT License.
//
// Data types of the REAN SDK core. Strings are UTF-8.
#pragma once

#include "Rean/ReanExport.h"

#include <cstdint>
#include <string>
#include <vector>

REAN_CORE_WARNINGS_PUSH

namespace rean
{

/** What went wrong. `None` means success. */
enum class ErrorCode : int
{
	None = 0,
	InvalidArgument,  // a parameter is empty, too long or malformed; nothing was sent
	InvalidConfig,    // the client id is missing or is not rean_ + 24 lowercase letters/digits
	NotSignedIn,      // sign in first (or the player signed out before the answer arrived)
	NoSavedSession,   // RestoreSession found nothing to restore
	Busy,             // a sign-in is already running, or a player is already signed in
	Cancelled,        // CancelSignIn / SignOut ended it
	Network,          // REAN could not be reached
	RateLimited,      // HTTP 429: wait and retry
	InvalidClient,    // the client id is unknown or the app is not active (401 invalid_client)
	InvalidGrant,     // the launcher code / authorization code is invalid, expired or used
	SessionEnded,     // the saved session ended (expired, revoked, app removed): sign in again
	Denied,           // the player declined, or REAN refused the request (403)
	Expired,          // the device code or the invite expired (expired_token, 410)
	MissingScope,     // the app did not ask for the scope this needs (insufficient_scope / invalid_scope)
	NotFriends,       // invites only go to friends (403 not_friends)
	NotFound,         // 404, e.g. an invite that isn't yours
	InvalidRequest,   // REAN rejected the request (400)
	Server,           // REAN failed (5xx) or answered something unexpected
	NotSupported,     // not available here (e.g. no secure random source for browser sign-in)
};

/** Stable English name of an error code, e.g. "session_ended". */
REAN_CORE_API const char* ErrorCodeName(ErrorCode Code);

struct REAN_CORE_API Error
{
	ErrorCode code = ErrorCode::None;
	int httpStatus = 0;       // 0 when no HTTP answer was involved
	std::string apiCode;      // machine code from REAN, e.g. "not_friends", "invalid_grant" (may be empty)
	std::string message;      // human-readable explanation (English)

	bool Ok() const { return code == ErrorCode::None; }
	static Error Make(ErrorCode Code, std::string Message);
};

/** A REAN player. `id` is the permanent player id (the OpenID `sub`): use it as the key. */
struct REAN_CORE_API User
{
	std::string id;
	std::string username;
	std::string displayName;
	std::string avatarUrl;      // may be empty
	std::string email;          // only with the email scope
	bool emailVerified = false;
};

enum class PresenceStatus : int
{
	Offline = 0,
	Online,
	Away,
	DoNotDisturb,
	InGame,
	Unknown,  // a status this SDK version does not know yet (see Presence::statusText)
};

REAN_CORE_API PresenceStatus ParsePresenceStatus(const std::string& Text);

struct REAN_CORE_API AppInfo
{
	std::string clientId;
	std::string name;
	std::string iconUrl;
};

/** What a friend is doing. Party and join information is only shared inside the same game. */
struct REAN_CORE_API FriendActivity
{
	AppInfo app;
	std::string details;
	std::string state;
	std::string startedAt;       // ISO 8601 as sent by REAN (empty when unknown)
	int64_t startedAtUnix = 0;   // seconds, 0 when unknown
	bool inYourGame = false;     // the friend is in your app: party / joinable are meaningful
	bool hasParty = false;
	std::string partyId;
	int partySize = -1;          // -1 when unknown
	int partyMax = -1;
	bool joinable = false;
};

struct REAN_CORE_API Presence
{
	PresenceStatus status = PresenceStatus::Offline;
	std::string statusText = "offline";  // online, away, dnd, in_game or offline
	bool hasActivity = false;
	FriendActivity activity;
};

struct REAN_CORE_API Friend
{
	User user;
	std::string friendsSince;    // ISO 8601, may be empty
	bool hasPresence = false;    // false without the presence.read scope
	Presence presence;
};

struct REAN_CORE_API PlayerPresence
{
	std::string playerId;
	Presence presence;
};

/** Rich presence you set for the signed-in player. */
struct REAN_CORE_API Activity
{
	std::string status;          // "" = in_game; or online, away, dnd
	std::string details;         // e.g. "Ranked" (at most 128 characters)
	std::string state;           // e.g. "Round 3" (at most 128 characters)
	std::string partyId;         // your lobby id (at most 128); only friends in your game see it
	int partySize = -1;          // 0..1000, -1 = not set
	int partyMax = -1;           // 0..1000, -1 = not set; partySize <= partyMax
	bool joinable = false;
	int64_t startedAt = 0;       // unix SECONDS, 0 = not set
};

struct REAN_CORE_API Invite
{
	std::string id;
	User from;
	std::string message;
	std::string joinSecret;
	std::string createdAt;       // ISO 8601
	std::string expiresAt;       // ISO 8601
};

struct REAN_CORE_API SentInvite
{
	std::string id;
	std::string to;
	std::string expiresAt;
};

enum class EventType : int
{
	InviteReceived = 0,
	InviteAccepted,
	InviteDeclined,
	Unknown,  // a newer event type: see Event::typeName and Event::rawData
};

struct REAN_CORE_API Event
{
	int64_t id = 0;
	EventType type = EventType::Unknown;
	std::string typeName;        // "invite.received", ...
	std::string createdAt;       // ISO 8601
	Invite invite;               // InviteReceived: the invite (with the inviter in invite.from)
	std::string inviteId;        // InviteAccepted / InviteDeclined: which of your invites
	User by;                     // InviteAccepted / InviteDeclined: the friend who answered
	std::string rawData;         // the event's `data` as JSON (for Unknown types)
};

/** Device sign-in: show userCode and verificationUri to the player. */
struct REAN_CORE_API DeviceCode
{
	std::string userCode;                // e.g. "BCDF-GHJK"
	std::string verificationUri;         // https://reanstudios.com/activate
	std::string verificationUriComplete; // same with the code filled in (good for a QR code)
	int expiresInSeconds = 600;
	int intervalSeconds = 5;
};

enum class SessionChange : int
{
	SignedIn = 0,
	SignedOut,     // SignOut() was called
	SessionEnded,  // REAN ended the session (revoked, expired, app removed): sign in again
};

} // namespace rean

REAN_CORE_WARNINGS_POP
