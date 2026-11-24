// Copyright (c) REAN Studios. MIT License.
//
// rean::Client: Sign in with REAN (REAN ID: OAuth 2.0 + OpenID Connect, public
// client) and the REAN Social API (friends, presence, rich presence, invites).
//
// Rules
//  - Call every method, and Tick(), from one thread (the game thread).
//  - Call Tick() every frame (or every 50-100 ms). Every callback runs inside
//    Tick(), never inside the call that started the work.
//  - Every callback passed to an asynchronous method is called exactly once
//    (with an Error when it failed), unless the Client is destroyed first.
//  - Never destroy the Client from inside one of its callbacks.
//  - Tokens are never logged.
#pragma once

#include "Rean/ReanExport.h"
#include "Rean/ReanPlatform.h"
#include "Rean/ReanTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

REAN_CORE_WARNINGS_PUSH

namespace rean
{

/** The scopes a game asks for by default. */
REAN_CORE_API extern const char* const DefaultScopes;
REAN_CORE_API extern const char* const DefaultApiBase;       // https://api.reanstudios.com
REAN_CORE_API extern const char* const DefaultAuthorizeUrl;  // https://reanstudios.com/oauth/authorize

struct REAN_CORE_API Config
{
	/** Your app's client id: "rean_" + 24 lowercase letters or digits. Public, not a secret. */
	std::string clientId;
	/** Space-separated scopes. Add offline_access to keep players signed in. */
	std::string scopes = DefaultScopes;
	/** https://api.reanstudios.com, or https://rean-client-vyli.vercel.app (same API). */
	std::string apiBase = DefaultApiBase;
	std::string authorizeUrl = DefaultAuthorizeUrl;
	/** Sent as User-Agent, e.g. "rean-sdk-unreal/0.1.0". */
	std::string userAgent = "rean-core/" REAN_CORE_VERSION;

	/** Keep the refresh token in `store` so RestoreSession works on the next launch. */
	bool saveSession = true;
	/** Appended to the storage key of the session (e.g. ".pie1" for a second PIE client). */
	std::string sessionKeySuffix;
	/** Seconds between event checks while an event callback is set (10..300). */
	int eventPollSeconds = 10;
	/** Invite answers older than this are not delivered (old feed entries after a restart). */
	int maxEventAgeSeconds = 15 * 60;

	std::shared_ptr<IHttpTransport> transport;  // required
	std::shared_ptr<IKeyValueStore> store;      // optional (null: nothing is saved)

	/** Monotonic milliseconds. Default: std::chrono::steady_clock. */
	std::function<int64_t()> monotonicMs;
	/** Wall clock, unix milliseconds. Default: std::chrono::system_clock. */
	std::function<int64_t()> unixMs;
	/** Cryptographically secure random bytes (browser sign-in only). Default: std::random_device. */
	std::function<bool(uint8_t* Out, size_t Count)> randomBytes;
	/** Log sink (never receives tokens). */
	std::function<void(LogLevel Level, const std::string& Message)> log;
};

using SignInCallback = std::function<void(const Error& Result, const User& Player)>;
using DeviceCodeCallback = std::function<void(const DeviceCode& Code)>;
using ResultCallback = std::function<void(const Error& Result)>;
using UserCallback = std::function<void(const Error& Result, const User& Player)>;
using FriendsCallback = std::function<void(const Error& Result, const std::vector<Friend>& Friends)>;
using PresenceCallback = std::function<void(const Error& Result, const std::vector<PlayerPresence>& Presence)>;
using InvitesCallback = std::function<void(const Error& Result, const std::vector<Invite>& Invites)>;
using SentInviteCallback = std::function<void(const Error& Result, const SentInvite& Invite)>;
using JoinCallback = std::function<void(const Error& Result, const std::string& JoinSecret)>;
using EventCallback = std::function<void(const Event& Event)>;
using SessionCallback = std::function<void(SessionChange Change, const Error& Reason)>;

class REAN_CORE_API Client
{
public:
	explicit Client(Config InConfig);
	~Client();
	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	/** Not Ok when the config is unusable (every call then fails with this error). */
	const Error& ConfigError() const;
	const Config& GetConfig() const;

	/** Delivers answers and runs timers (device polling, presence heartbeat, events). */
	void Tick();

	// ── Sign in ─────────────────────────────────────────────────────────────

	/** Exchanges the one-time -ReanAuthCode=rlc_… the REAN Launcher passed. A code is never sent twice. */
	void SignInWithLauncher(const std::string& LauncherCode, SignInCallback Done);
	/** Device sign-in: OnCode gets the code to show; Done runs when the player allowed (or not). */
	void SignInWithDevice(DeviceCodeCallback OnCode, SignInCallback Done);
	/** Signs back in with the refresh token saved in the store. NoSavedSession when there is none. */
	void RestoreSession(SignInCallback Done);
	/** Signs in with a refresh token you stored yourself. */
	void SignInWithRefreshToken(const std::string& RefreshToken, SignInCallback Done);

	/**
	 * Browser sign-in (authorization code + PKCE S256). Builds the URL to open
	 * in the system browser; the browser comes back to RedirectUri (e.g.
	 * http://127.0.0.1:53117/callback). Pass what arrives there to
	 * CompleteBrowserSignIn. Returns an error (and never calls Done) when it
	 * could not start.
	 */
	Error BeginBrowserSignIn(const std::string& RedirectUri, std::string& OutAuthorizeUrl, SignInCallback Done);
	/** The full callback URL or just its query. Wrong `state` is refused and the sign-in keeps waiting. */
	Error CompleteBrowserSignIn(const std::string& CallbackUrlOrQuery);

	void CancelSignIn();
	/** Clears presence, revokes the session on REAN, forgets the saved session. */
	void SignOut();
	/** Call when the game exits: clears the presence this game set (best effort). */
	void Shutdown();

	bool IsSignedIn() const;
	bool IsSigningIn() const;
	/** Empty fields when signed out. Filled from the ID token (needs openid, profile). */
	const User& CurrentUser() const;
	/** Scopes REAN granted (space-separated). */
	const std::string& GrantedScopes() const;
	bool HasScope(const std::string& Scope) const;
	/** The current refresh token (rotates on every refresh), empty without offline_access. */
	const std::string& RefreshToken() const;
	/** Sign-in, sign-out and session-ended notifications. */
	void SetSessionCallback(SessionCallback Callback);

	// ── Social API ──────────────────────────────────────────────────────────

	void GetMe(UserCallback Done);                       // profile
	void GetFriends(FriendsCallback Done);               // friends.read (+ presence.read)
	void GetPresence(const std::vector<std::string>& PlayerIds, PresenceCallback Done);  // presence.read, <= 100 ids

	/**
	 * Shows what the player is doing (presence.write) and keeps it alive: REAN
	 * forgets presence after 120 s, so it is re-sent every 60 s until
	 * ClearActivity / SignOut. Returns InvalidArgument for bad values. OnFirstSend
	 * (optional) gets the result of the first send.
	 */
	Error SetActivity(const Activity& NewActivity, ResultCallback OnFirstSend = nullptr);
	void ClearActivity();
	bool HasActivity() const;

	void GetInvites(InvitesCallback Done);               // invites
	void SendInvite(const std::string& FriendId, const std::string& JoinSecret, const std::string& Message, SentInviteCallback Done);
	void RespondToInvite(const std::string& InviteId, bool bAccept, JoinCallback Done);

	/** invite.received / invite.accepted / invite.declined. Polls while set and signed in; null stops. */
	void SetEventCallback(EventCallback Callback);
	void SetEventPollSeconds(int Seconds);

	struct Impl;

private:
	std::unique_ptr<Impl> P;
};

// ── Helpers ────────────────────────────────────────────────────────────────

/** "rean_" + 24 characters a-z / 0-9. */
REAN_CORE_API bool IsValidClientId(const std::string& ClientId);
/** Canonical UUID (8-4-4-4-12 hex). */
REAN_CORE_API bool IsUuid(const std::string& Text);
/** Value of -ReanAuthCode=… / -ReanAuthCode … / --rean-auth-code=… in argv-style arguments, or "". */
REAN_CORE_API std::string FindLauncherCode(const std::vector<std::string>& Args);
/** Number of UTF-16 code units (how REAN counts the 128 / 140 / 256 limits). */
REAN_CORE_API size_t Utf16Length(const std::string& Utf8);

/** Loopback redirect helpers for browser sign-in: the request target of an HTTP request head. */
REAN_CORE_API bool ParseHttpRequestTarget(const std::string& RequestHead, std::string& OutMethod, std::string& OutTarget);
/** A complete small HTTP/1.1 response page for the loopback listener. */
REAN_CORE_API std::string LoopbackResponse(bool bSignedIn);

} // namespace rean

REAN_CORE_WARNINGS_POP
