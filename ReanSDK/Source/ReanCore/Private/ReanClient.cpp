// Copyright (c) REAN Studios. MIT License.
//
// rean::Client implementation. Behaviour follows the tested REAN C SDK:
//  - every answer is queued and handled in Tick (callbacks never run inside the call that started them);
//  - a generation counter, bumped on every sign-in start, cancel, sign-out and session end, makes late
//    answers to an old sign-in / refresh change nothing;
//  - one refresh at a time; requests wait for it; a 401 refreshes once, a second 401 ends the session;
//  - refresh tokens rotate: the new one is saved before anything else; invalid_grant ends the session;
//  - presence is re-sent every 60 s while an activity is set (REAN forgets it after 120 s);
//  - events are polled while a listener is set, with a persisted cursor.
#include "Rean/ReanClient.h"

#include "ReanCrypto.h"
#include "ReanJson.h"
#include "ReanText.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>

namespace rean
{

const char* const DefaultScopes = "openid profile offline_access friends.read presence.read presence.write invites";
const char* const DefaultApiBase = "https://api.reanstudios.com";
const char* const DefaultAuthorizeUrl = "https://reanstudios.com/oauth/authorize";

namespace
{
const char* const DeviceGrantType = "urn:ietf:params:oauth:grant-type:device_code";
const char* const LauncherGrantType = "urn:rean:params:oauth:grant-type:launcher_code";

constexpr int64_t RefreshMarginMs = 60 * 1000;      // refresh a minute before the access token expires
constexpr int64_t HeartbeatMs = 60 * 1000;          // presence lives 120 s on REAN
constexpr int64_t HeartbeatRetryMs = 15 * 1000;     // after a network / server failure
constexpr int64_t PresenceMinGapMs = 5 * 1000;      // 120 updates / 10 min per player
constexpr int64_t BrowserTimeoutMs = 10 * 60 * 1000;
constexpr int64_t MaxDeviceIntervalMs = 60 * 1000;
constexpr size_t MaxActivityText = 128;
constexpr size_t MaxJoinSecret = 256;
constexpr size_t MaxInviteMessage = 140;
constexpr size_t MaxPresenceIds = 100;
constexpr size_t EventsPageSize = 100;

struct Inbox
{
	std::mutex Mutex;
	std::vector<std::pair<uint64_t, HttpResponse>> Done;
};

// A launcher code works once; presenting it again makes REAN revoke what the first use produced.
// Remembered per process so a second Client (e.g. another PIE instance) cannot resend it either.
std::mutex& LauncherCodeMutex()
{
	static std::mutex M;
	return M;
}

std::set<std::string>& UsedLauncherCodes()
{
	static std::set<std::string> Codes;
	return Codes;
}

int64_t SteadyNowMs()
{
	return static_cast<int64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

int64_t SystemNowMs()
{
	return static_cast<int64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool DefaultRandomBytes(uint8_t* Out, size_t Count)
{
	// std::random_device is backed by the OS CSPRNG on MSVC (rand_s), libstdc++ and libc++.
	std::random_device Device;
	size_t I = 0;
	while (I < Count)
	{
		const uint32_t V = static_cast<uint32_t>(Device());
		for (int K = 0; K < 4 && I < Count; K++) Out[I++] = static_cast<uint8_t>(V >> (8 * K));
	}
	return true;
}

std::string TrimSpaces(const std::string& Text)
{
	size_t Begin = 0;
	size_t End = Text.size();
	while (Begin < End && (Text[Begin] == ' ' || Text[Begin] == '\t' || Text[Begin] == '\n' || Text[Begin] == '\r')) Begin++;
	while (End > Begin && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '\n' || Text[End - 1] == '\r')) End--;
	return Text.substr(Begin, End - Begin);
}

bool HasControlOrSpace(const std::string& Text)
{
	for (const char C : Text)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		if (U <= 0x20 || U == 0x7F) return true;
	}
	return false;
}

bool ContainsScope(const std::string& Scopes, const std::string& Scope)
{
	size_t Start = 0;
	while (Start < Scopes.size())
	{
		size_t End = Scopes.find(' ', Start);
		if (End == std::string::npos) End = Scopes.size();
		if (Scopes.compare(Start, End - Start, Scope) == 0) return true;
		Start = End + 1;
	}
	return false;
}

const char* DefaultMessage(ErrorCode Code)
{
	switch (Code)
	{
	case ErrorCode::None: return "";
	case ErrorCode::InvalidArgument: return "Invalid argument.";
	case ErrorCode::InvalidConfig: return "The REAN SDK is not configured.";
	case ErrorCode::NotSignedIn: return "Sign in with REAN first.";
	case ErrorCode::NoSavedSession: return "No saved REAN session.";
	case ErrorCode::Busy: return "A sign-in is already running.";
	case ErrorCode::Cancelled: return "Cancelled.";
	case ErrorCode::Network: return "Could not reach REAN. Check the connection.";
	case ErrorCode::RateLimited: return "Too many requests. Wait a moment and try again.";
	case ErrorCode::InvalidClient: return "REAN does not know this client id, or the app is not active.";
	case ErrorCode::InvalidGrant: return "The sign-in code is invalid, expired or was already used.";
	case ErrorCode::SessionEnded: return "The REAN session ended. Sign in again.";
	case ErrorCode::Denied: return "REAN refused the request.";
	case ErrorCode::Expired: return "It expired.";
	case ErrorCode::MissingScope: return "The app did not ask for the permission (scope) this needs.";
	case ErrorCode::NotFriends: return "Invites only go to REAN friends.";
	case ErrorCode::NotFound: return "Not found.";
	case ErrorCode::InvalidRequest: return "REAN rejected the request.";
	case ErrorCode::Server: return "REAN had a problem. Try again later.";
	case ErrorCode::NotSupported: return "Not supported on this platform.";
	}
	return "Error.";
}

/** Maps an HTTP answer to an Error. OAuth endpoints answer { error, error_description }; the
 *  Social API answers { error: "<message>", code? } and, for token problems (RFC 6750),
 *  { error: "invalid_token" | "insufficient_scope", error_description }. */
Error MapHttpError(int Status, const Json& Body)
{
	Error E;
	E.httpStatus = Status;
	if (Status >= 200 && Status < 300) return E;
	if (Status == 0)
	{
		E.code = ErrorCode::Network;
		E.message = DefaultMessage(E.code);
		return E;
	}
	const std::string ErrorField = Body["error"].Str();
	const std::string Description = Body["error_description"].Str();
	const std::string CodeField = Body["code"].Str();
	if (!CodeField.empty()) E.apiCode = CodeField;
	else if (IsMachineCode(ErrorField)) E.apiCode = ErrorField;
	if (!Description.empty()) E.message = Description;
	else if (!ErrorField.empty() && !IsMachineCode(ErrorField)) E.message = ErrorField;

	const std::string& A = E.apiCode;
	if (A == "invalid_client") E.code = ErrorCode::InvalidClient;
	else if (A == "invalid_grant") E.code = ErrorCode::InvalidGrant;
	else if (A == "access_denied" || A == "consent_required") E.code = ErrorCode::Denied;
	else if (A == "expired_token" || A == "expired") E.code = ErrorCode::Expired;
	else if (A == "invalid_scope" || A == "insufficient_scope") E.code = ErrorCode::MissingScope;
	else if (A == "not_friends") E.code = ErrorCode::NotFriends;
	else if (A == "slow_down" || Status == 429) E.code = ErrorCode::RateLimited;
	else if (A == "invalid_token") E.code = ErrorCode::SessionEnded;
	else if (A == "temporarily_unavailable") E.code = ErrorCode::Server;
	else if (A == "invalid_request" || A == "unsupported_grant_type") E.code = ErrorCode::InvalidRequest;
	else if (Status == 400) E.code = ErrorCode::InvalidRequest;
	else if (Status == 401) E.code = ErrorCode::SessionEnded;
	else if (Status == 403) E.code = ErrorCode::Denied;
	else if (Status == 404) E.code = ErrorCode::NotFound;
	else if (Status == 410) E.code = ErrorCode::Expired;
	else E.code = ErrorCode::Server;
	if (E.message.empty()) E.message = DefaultMessage(E.code);
	return E;
}

bool JwtClaims(const std::string& Jwt, Json& Out)
{
	const size_t A = Jwt.find('.');
	if (A == std::string::npos) return false;
	const size_t B = Jwt.find('.', A + 1);
	if (B == std::string::npos) return false;
	std::string Payload;
	if (!Base64UrlDecode(Jwt.substr(A + 1, B - A - 1), Payload)) return false;
	return Json::Parse(Payload, Out) && Out.IsObject();
}

bool AudienceIncludes(const Json& Aud, const std::string& ClientId)
{
	if (Aud.IsString()) return Aud.Str() == ClientId;
	for (size_t I = 0; I < Aud.Size(); I++)
	{
		if (Aud.At(I).Str() == ClientId) return true;
	}
	return false;
}

User ParseUser(const Json& J)
{
	User U;
	U.id = J["id"].Str();
	U.username = J["username"].Str();
	U.displayName = J["displayName"].Str(U.username);
	if (U.displayName.empty()) U.displayName = U.username;
	U.avatarUrl = J["avatarUrl"].Str();
	U.email = J["email"].Str();
	return U;
}

int64_t TimeToUnixSeconds(const Json& J)
{
	if (J.IsNumber()) return J.Int(0);
	int64_t Ms = 0;
	return ParseIso8601Ms(J.Str(), Ms) ? Ms / 1000 : 0;
}

Presence ParsePresence(const Json& J)
{
	Presence P;
	P.statusText = J["status"].Str("offline");
	P.status = ParsePresenceStatus(P.statusText);
	const Json& A = J["activity"];
	if (A.IsObject())
	{
		P.hasActivity = true;
		FriendActivity& F = P.activity;
		F.app.clientId = A["app"]["clientId"].Str();
		F.app.name = A["app"]["name"].Str();
		F.app.iconUrl = A["app"]["iconUrl"].Str();
		F.details = A["details"].Str();
		F.state = A["state"].Str();
		F.startedAt = A["startedAt"].IsString() ? A["startedAt"].Str() : std::string();
		F.startedAtUnix = TimeToUnixSeconds(A["startedAt"]);
		// REAN adds party / joinable only when the viewer's app is the friend's app.
		F.inYourGame = A.Has("party") || A.Has("joinable");
		const Json& Party = A["party"];
		if (Party.IsObject())
		{
			F.hasParty = true;
			F.partyId = Party["id"].Str();
			F.partySize = static_cast<int>(Party["size"].Int(-1));
			F.partyMax = static_cast<int>(Party["max"].Int(-1));
		}
		F.joinable = A["joinable"].Bool(false);
	}
	return P;
}

Invite ParseInvite(const Json& J)
{
	Invite I;
	I.id = J["id"].Str();
	I.from = ParseUser(J["from"]);
	I.message = J["message"].Str();
	I.joinSecret = J["joinSecret"].Str();
	I.createdAt = J["createdAt"].Str();
	I.expiresAt = J["expiresAt"].Str();
	return I;
}

Event ParseEvent(const Json& J)
{
	Event E;
	E.id = J["id"].Int(0);
	E.typeName = J["type"].Str();
	E.createdAt = J["createdAt"].Str();
	const Json& Data = J["data"];
	E.rawData = Data.Dump();
	if (E.typeName == "invite.received")
	{
		E.type = EventType::InviteReceived;
		E.invite = ParseInvite(Data);
	}
	else if (E.typeName == "invite.accepted" || E.typeName == "invite.declined")
	{
		E.type = E.typeName == "invite.accepted" ? EventType::InviteAccepted : EventType::InviteDeclined;
		E.inviteId = Data["id"].Str();
		E.by = ParseUser(Data["by"]);
	}
	return E;
}

Error Invalid(const std::string& Message)
{
	return Error::Make(ErrorCode::InvalidArgument, Message);
}
} // namespace

// ─── Small public types ─────────────────────────────────────────────────────

const char* ErrorCodeName(ErrorCode Code)
{
	switch (Code)
	{
	case ErrorCode::None: return "none";
	case ErrorCode::InvalidArgument: return "invalid_argument";
	case ErrorCode::InvalidConfig: return "invalid_config";
	case ErrorCode::NotSignedIn: return "not_signed_in";
	case ErrorCode::NoSavedSession: return "no_saved_session";
	case ErrorCode::Busy: return "busy";
	case ErrorCode::Cancelled: return "cancelled";
	case ErrorCode::Network: return "network";
	case ErrorCode::RateLimited: return "rate_limited";
	case ErrorCode::InvalidClient: return "invalid_client";
	case ErrorCode::InvalidGrant: return "invalid_grant";
	case ErrorCode::SessionEnded: return "session_ended";
	case ErrorCode::Denied: return "denied";
	case ErrorCode::Expired: return "expired";
	case ErrorCode::MissingScope: return "missing_scope";
	case ErrorCode::NotFriends: return "not_friends";
	case ErrorCode::NotFound: return "not_found";
	case ErrorCode::InvalidRequest: return "invalid_request";
	case ErrorCode::Server: return "server";
	case ErrorCode::NotSupported: return "not_supported";
	}
	return "unknown";
}

Error Error::Make(ErrorCode Code, std::string Message)
{
	Error E;
	E.code = Code;
	E.message = Message.empty() ? std::string(DefaultMessage(Code)) : std::move(Message);
	return E;
}

PresenceStatus ParsePresenceStatus(const std::string& Text)
{
	if (Text == "offline" || Text.empty()) return PresenceStatus::Offline;
	if (Text == "online") return PresenceStatus::Online;
	if (Text == "away") return PresenceStatus::Away;
	if (Text == "dnd") return PresenceStatus::DoNotDisturb;
	if (Text == "in_game") return PresenceStatus::InGame;
	return PresenceStatus::Unknown;
}

std::string HttpResponse::Header(const std::string& Name) const
{
	const std::string Want = ToLowerAscii(Name);
	for (const HttpHeader& H : headers)
	{
		if (ToLowerAscii(H.name) == Want) return H.value;
	}
	return std::string();
}

IHttpTransport::~IHttpTransport() = default;
IKeyValueStore::~IKeyValueStore() = default;

bool MemoryStore::Load(const std::string& Key, std::string& OutValue)
{
	const auto It = Values.find(Key);
	if (It == Values.end()) return false;
	OutValue = It->second;
	return true;
}

bool MemoryStore::Save(const std::string& Key, const std::string& Value)
{
	Values[Key] = Value;
	return true;
}

void MemoryStore::Remove(const std::string& Key)
{
	Values.erase(Key);
}

// ─── Helpers ────────────────────────────────────────────────────────────────

bool IsValidClientId(const std::string& ClientId)
{
	if (ClientId.size() != 29 || ClientId.compare(0, 5, "rean_") != 0) return false;
	for (size_t I = 5; I < ClientId.size(); I++)
	{
		const char C = ClientId[I];
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))) return false;
	}
	return true;
}

bool IsUuid(const std::string& Text)
{
	if (Text.size() != 36) return false;
	for (size_t I = 0; I < Text.size(); I++)
	{
		const char C = Text[I];
		if (I == 8 || I == 13 || I == 18 || I == 23)
		{
			if (C != '-') return false;
		}
		else if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F')))
		{
			return false;
		}
	}
	return true;
}

std::string FindLauncherCode(const std::vector<std::string>& Args)
{
	static const char* const Names[] = { "-reanauthcode", "--rean-auth-code" };
	for (size_t I = 0; I < Args.size(); I++)
	{
		const std::string Lower = ToLowerAscii(Args[I]);
		for (const char* Name : Names)
		{
			const std::string N = Name;
			if (Lower.compare(0, N.size(), N) != 0) continue;
			std::string Value;
			if (Lower.size() > N.size() && Lower[N.size()] == '=') Value = Args[I].substr(N.size() + 1);
			else if (Lower.size() == N.size() && I + 1 < Args.size()) Value = Args[I + 1];
			else continue;
			if (Value.size() >= 2 && Value.front() == '"' && Value.back() == '"') Value = Value.substr(1, Value.size() - 2);
			return Value;
		}
	}
	return std::string();
}

size_t Utf16Length(const std::string& Utf8)
{
	size_t Units = 0;
	for (const char C : Utf8)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		if ((U & 0xC0) == 0x80) continue;  // continuation byte
		Units += (U >= 0xF0) ? 2 : 1;       // 4-byte sequences are surrogate pairs in UTF-16
	}
	return Units;
}

bool ParseHttpRequestTarget(const std::string& RequestHead, std::string& OutMethod, std::string& OutTarget)
{
	const size_t LineEnd = RequestHead.find("\r\n");
	const std::string Line = RequestHead.substr(0, LineEnd);
	const size_t A = Line.find(' ');
	if (A == std::string::npos || A == 0) return false;
	const size_t B = Line.find(' ', A + 1);
	if (B == std::string::npos || B == A + 1) return false;
	if (Line.compare(B + 1, 5, "HTTP/") != 0) return false;
	OutMethod = Line.substr(0, A);
	OutTarget = Line.substr(A + 1, B - A - 1);
	return true;
}

std::string LoopbackResponse(bool bSignedIn)
{
	const std::string Title = bSignedIn ? "Signed in with REAN" : "Sign-in did not finish";
	const std::string Text = bSignedIn ? "You can close this tab and go back to the game."
	                                   : "Go back to the game and try again.";
	const std::string Page = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + Title +
	                         "</title></head><body style=\"font-family:system-ui,sans-serif;background:#000;color:#fff;"
	                         "display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
	                         "<div style=\"text-align:center\"><h1>" + Title + "</h1><p>" + Text + "</p></div></body></html>";
	return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
	       "Content-Length: " + std::to_string(Page.size()) + "\r\n\r\n" + Page;
}

// ─── Implementation ─────────────────────────────────────────────────────────

struct Reply
{
	int status = 0;         // HTTP status; 0 network failure; -1 not sent (local error)
	Json body;
	int retryAfterSeconds = 0;
	Error error;            // Ok for 2xx answers
};

struct Client::Impl
{
	enum class SignKind
	{
		Idle,
		Token,
		Device,
		Browser,
	};

	struct Request
	{
		uint64_t id = 0;
		bool api = false;       // Social API: Bearer token, refresh once on 401
		unsigned gen = 0;
		std::string method;
		std::string path;
		std::string body;
		bool jsonBody = false;
		bool retried = false;
		std::function<void(Reply&)> handler;
	};

	Config Cfg;
	Error ConfigErr;
	std::shared_ptr<Inbox> Box = std::make_shared<Inbox>();
	uint64_t NextRequestId = 1;
	unsigned Gen = 1;
	std::unordered_map<uint64_t, Request> InFlight;
	std::vector<Request> Waiting;                 // waiting for a fresh access token
	std::vector<std::function<void()>> Deferred;  // user callbacks for the next Tick pass

	// Session
	bool bSignedIn = false;
	std::string AccessToken;
	int64_t AccessExpiresAt = 0;
	std::string RefreshTokenValue;
	std::string ScopeList;
	User Player;
	SessionCallback OnSession;
	bool bRefreshing = false;
	int64_t RefreshRetryAt = 0;
	int64_t RefreshBackoffMs = 0;
	Error LastRefreshError;

	// Sign-in in progress
	SignKind Signing = SignKind::Idle;
	SignInCallback SignInDone;
	DeviceCodeCallback OnDeviceCode;
	std::string DeviceCodeValue;
	int64_t DeviceIntervalMs = 5000;
	int64_t DeviceNextPoll = 0;
	int64_t DeviceDeadline = 0;
	bool bDevicePolling = false;
	std::string BrowserState;
	std::string BrowserNonce;
	std::string BrowserVerifier;
	std::string BrowserRedirect;
	int64_t BrowserDeadline = 0;
	bool bBrowserExchanging = false;

	// Rich presence
	bool bHasActivity = false;
	std::string ActivityBody;
	int64_t NextHeartbeat = 0;
	int64_t LastPresenceSent = 0;
	bool bPresenceSentOnce = false;
	bool bPresenceInFlight = false;
	bool bClearAfterInFlight = false;
	ResultCallback FirstSend;

	// Events
	EventCallback OnEvent;
	int64_t NextEvents = 0;
	bool bEventsInFlight = false;
	int64_t Cursor = 0;
	int64_t EventsBackoffMs = 0;
	int64_t ServerOffsetMs = 0;

	explicit Impl(Config InConfig) : Cfg(std::move(InConfig)) {}

	// ── basics ──

	int64_t Now() const { return Cfg.monotonicMs(); }
	int64_t ServerNowMs() const { return Cfg.unixMs() + ServerOffsetMs; }

	void Log(LogLevel Level, const std::string& Message) const
	{
		if (Cfg.log) Cfg.log(Level, Message);
	}

	std::string SessionKey() const { return "rean.session." + Cfg.clientId + Cfg.sessionKeySuffix; }
	std::string CursorKey() const { return "rean.events." + Cfg.clientId + "." + Player.id; }

	void Defer(std::function<void()> Fn) { Deferred.push_back(std::move(Fn)); }

	void RunDeferred()
	{
		std::vector<std::function<void()>> Batch;
		Batch.swap(Deferred);
		for (auto& Fn : Batch) Fn();
	}

	void Validate()
	{
		Cfg.clientId = TrimSpaces(Cfg.clientId);
		Cfg.scopes = TrimSpaces(Cfg.scopes);
		if (Cfg.scopes.empty()) Cfg.scopes = DefaultScopes;
		Cfg.apiBase = TrimSpaces(Cfg.apiBase);
		while (!Cfg.apiBase.empty() && Cfg.apiBase.back() == '/') Cfg.apiBase.pop_back();
		if (Cfg.authorizeUrl.empty()) Cfg.authorizeUrl = DefaultAuthorizeUrl;
		Cfg.eventPollSeconds = std::max(10, std::min(300, Cfg.eventPollSeconds));
		if (Cfg.maxEventAgeSeconds < 60) Cfg.maxEventAgeSeconds = 60;
		if (!Cfg.monotonicMs) Cfg.monotonicMs = SteadyNowMs;
		if (!Cfg.unixMs) Cfg.unixMs = SystemNowMs;
		if (!Cfg.randomBytes) Cfg.randomBytes = DefaultRandomBytes;

		const bool bLocal = Cfg.apiBase.compare(0, 17, "http://127.0.0.1:") == 0 || Cfg.apiBase.compare(0, 17, "http://localhost:") == 0;
		if (!Cfg.transport)
		{
			ConfigErr = Error::Make(ErrorCode::InvalidConfig, "No HTTP transport.");
		}
		else if (!IsValidClientId(Cfg.clientId))
		{
			ConfigErr = Error::Make(ErrorCode::InvalidConfig,
				"Set your REAN client id (\"rean_\" + 24 lowercase letters or digits) from the developer console.");
		}
		else if (Cfg.apiBase.compare(0, 8, "https://") != 0 && !bLocal)
		{
			ConfigErr = Error::Make(ErrorCode::InvalidConfig, "ApiBase must be an https:// address.");
		}
		if (ConfigErr.Ok() && Cfg.saveSession && !ContainsScope(Cfg.scopes, "offline_access"))
		{
			Log(LogLevel::Warning, "The scopes do not include offline_access: REAN gives no refresh token, so the session cannot be saved.");
		}
	}

	// ── HTTP plumbing ──

	Request NewRequest(const char* Method, std::string Path, std::string Body, bool bJson, std::function<void(Reply&)> Handler)
	{
		Request R;
		R.id = NextRequestId++;
		R.gen = Gen;
		R.method = Method;
		R.path = std::move(Path);
		R.body = std::move(Body);
		R.jsonBody = bJson;
		R.handler = std::move(Handler);
		return R;
	}

	void Transmit(Request R, const std::string& Bearer)
	{
		HttpRequest H;
		H.method = R.method;
		H.url = Cfg.apiBase + "/api/" + R.path;
		H.headers.push_back({ "Accept", "application/json" });
		H.headers.push_back({ "User-Agent", Cfg.userAgent });
		if (!R.body.empty())
		{
			H.headers.push_back({ "Content-Type", R.jsonBody ? "application/json" : "application/x-www-form-urlencoded" });
			H.body = R.body;
		}
		if (!Bearer.empty()) H.headers.push_back({ "Authorization", "Bearer " + Bearer });
		const uint64_t Id = R.id;
		InFlight.emplace(Id, std::move(R));
		std::weak_ptr<Inbox> WeakBox = Box;
		Cfg.transport->Send(H, [WeakBox, Id](HttpResponse Response) {
			if (std::shared_ptr<Inbox> B = WeakBox.lock())
			{
				std::lock_guard<std::mutex> Lock(B->Mutex);
				B->Done.emplace_back(Id, std::move(Response));
			}
		});
	}

	void OAuthPost(const char* Path, const KeyValues& Fields, std::function<void(Reply&)> Handler)
	{
		Transmit(NewRequest("POST", Path, FormEncode(Fields), false, std::move(Handler)), std::string());
	}

	void FailLater(std::function<void(Reply&)> Handler, const Error& E)
	{
		Defer([Handler, E]() {
			Reply R;
			R.status = -1;
			R.error = E;
			if (Handler) Handler(R);
		});
	}

	/** A sign-in answer that arrived after the sign-in was cancelled: end that session on REAN too. */
	void RevokeOrphan(const Reply& Rp)
	{
		const std::string Orphan = Rp.error.Ok() ? Rp.body["refresh_token"].Str() : std::string();
		if (!Orphan.empty()) OAuthPost("oauth/revoke", { { "token", Orphan }, { "client_id", Cfg.clientId } }, nullptr);
	}

	/** Authenticated Social API call. */
	void Api(const char* Method, std::string Path, std::string Body, std::function<void(Reply&)> Handler)
	{
		if (!ConfigErr.Ok()) return FailLater(std::move(Handler), ConfigErr);
		if (!bSignedIn) return FailLater(std::move(Handler), Error::Make(ErrorCode::NotSignedIn, ""));
		Request R = NewRequest(Method, std::move(Path), std::move(Body), true, std::move(Handler));
		R.api = true;
		Dispatch(std::move(R));
	}

	void Dispatch(Request R)
	{
		if (!AccessToken.empty() && Now() < AccessExpiresAt - RefreshMarginMs)
		{
			Transmit(std::move(R), AccessToken);
			return;
		}
		if (RefreshTokenValue.empty())
		{
			// No offline_access: the session ends with its access token.
			const Error E = Error::Make(ErrorCode::SessionEnded, "The REAN session expired. Sign in again.");
			FailLater(std::move(R.handler), E);
			DropSession(SessionChange::SessionEnded, E);
			return;
		}
		if (!bRefreshing && Now() < RefreshRetryAt)
		{
			FailLater(std::move(R.handler), LastRefreshError);
			return;
		}
		Waiting.push_back(std::move(R));
		StartRefresh();
	}

	void FlushWaiting(const Error& Failure)
	{
		std::vector<Request> List;
		List.swap(Waiting);
		for (Request& R : List)
		{
			if (Failure.Ok() && R.gen == Gen && !AccessToken.empty()) Transmit(std::move(R), AccessToken);
			else FailLater(std::move(R.handler), Failure.Ok() ? Error::Make(ErrorCode::NotSignedIn, "") : Failure);
		}
	}

	Reply MakeReply(HttpResponse& Response)
	{
		Reply R;
		R.status = Response.status;
		if (!Response.body.empty() && !Json::Parse(Response.body, R.body)) R.body = Json();
		R.error = MapHttpError(Response.status, R.body);
		if (R.error.Ok() && !R.body.IsObject())
		{
			R.error = Error::Make(ErrorCode::Server, "REAN sent an answer this SDK could not read.");
			R.error.httpStatus = Response.status;
		}
		const std::string RetryAfter = Response.Header("Retry-After");
		if (!RetryAfter.empty() && RetryAfter.size() < 7 && RetryAfter.find_first_not_of("0123456789") == std::string::npos)
		{
			R.retryAfterSeconds = std::stoi(RetryAfter);
		}
		int64_t ServerMs = 0;
		if (Response.status > 0 && ParseHttpDateMs(Response.Header("Date"), ServerMs)) ServerOffsetMs = ServerMs - Cfg.unixMs();
		return R;
	}

	void HandleCompletion(uint64_t Id, HttpResponse& Response)
	{
		auto It = InFlight.find(Id);
		if (It == InFlight.end()) return;
		Request R = std::move(It->second);
		InFlight.erase(It);
		Reply Rp = MakeReply(Response);
		if (R.api)
		{
			if (R.gen != Gen)
			{
				Rp.status = -1;
				Rp.error = Error::Make(ErrorCode::NotSignedIn, "The player signed out before REAN answered.");
			}
			else if (Rp.status == 401)
			{
				if (!R.retried && !RefreshTokenValue.empty())
				{
					// The access token was refused: refresh once and send again.
					R.retried = true;
					AccessToken.clear();
					Waiting.push_back(std::move(R));
					StartRefresh();
					return;
				}
				Rp.error.code = ErrorCode::SessionEnded;
				Rp.error.message = "REAN ended the session (" + Rp.error.message + "). Sign in again.";
				DropSession(SessionChange::SessionEnded, Rp.error);
			}
		}
		if (R.handler) R.handler(Rp);
	}

	// ── tokens and session ──

	enum class Grant
	{
		Code,     // launcher, device, authorization code
		Refresh,  // refresh_token
	};

	/** Reads a token answer. Checks first, changes state only when everything is fine. */
	Error StoreTokens(const Json& B, Grant Kind, const std::string& ExpectedNonce)
	{
		const std::string Access = B["access_token"].Str();
		if (Access.empty()) return Error::Make(ErrorCode::Server, "REAN's answer had no access token.");
		Json Claims;
		bool bIdToken = false;
		const std::string IdToken = B["id_token"].Str();
		if (!IdToken.empty())
		{
			if (!JwtClaims(IdToken, Claims)) return Error::Make(ErrorCode::Server, "The ID token could not be read.");
			if (!AudienceIncludes(Claims["aud"], Cfg.clientId))
			{
				return Error::Make(ErrorCode::InvalidGrant, "The ID token was issued to another app.");
			}
			if (!ExpectedNonce.empty() && Claims["nonce"].Str() != ExpectedNonce)
			{
				return Error::Make(ErrorCode::InvalidGrant, "The ID token's nonce does not match this sign-in.");
			}
			bIdToken = true;
		}
		else if (!JwtClaims(Access, Claims))
		{
			Claims = Json();
		}
		const std::string Sub = Claims["sub"].Str();
		if (Kind == Grant::Refresh && bSignedIn && !Sub.empty() && !Player.id.empty() && Sub != Player.id)
		{
			return Error::Make(ErrorCode::Server, "REAN returned a session for a different player.");
		}

		AccessToken = Access;
		const int64_t ExpiresIn = std::max<int64_t>(60, std::min<int64_t>(B["expires_in"].Int(3600), 24 * 3600));
		AccessExpiresAt = Now() + ExpiresIn * 1000;
		if (B["scope"].IsString()) ScopeList = B["scope"].Str();
		else if (ScopeList.empty()) ScopeList = Cfg.scopes;

		const std::string NewRefresh = B["refresh_token"].Str();
		if (!NewRefresh.empty())
		{
			// Refresh tokens rotate: the old one is spent now. Save the new one first.
			RefreshTokenValue = NewRefresh;
			if (Cfg.saveSession && Cfg.store && !Cfg.store->Save(SessionKey(), NewRefresh))
			{
				Log(LogLevel::Warning, "Could not save the REAN session; the player will have to sign in again next time.");
			}
		}
		else
		{
			RefreshTokenValue.clear();
			if (Cfg.store) Cfg.store->Remove(SessionKey());
		}

		if (!Sub.empty()) Player.id = Sub;
		if (bIdToken)
		{
			if (Claims.Has("preferred_username")) Player.username = Claims["preferred_username"].Str();
			if (Claims.Has("name")) Player.displayName = Claims["name"].Str();
			if (Player.displayName.empty()) Player.displayName = Player.username;
			if (Claims.Has("picture") || Kind == Grant::Code) Player.avatarUrl = Claims["picture"].Str();
			if (Claims.Has("email")) Player.email = Claims["email"].Str();
			if (Claims.Has("email_verified")) Player.emailVerified = Claims["email_verified"].Bool(false);
		}
		return Error();
	}

	void NotifySession(SessionChange Change, const Error& Reason)
	{
		Defer([this, Change, Reason]() {
			SessionCallback Cb = OnSession;  // copy: the callback may replace itself
			if (Cb) Cb(Change, Reason);
		});
	}

	void DropSession(SessionChange Change, const Error& Reason)
	{
		const bool bWas = bSignedIn;
		Gen++;
		bRefreshing = false;
		RefreshRetryAt = 0;
		RefreshBackoffMs = 0;
		bSignedIn = false;
		AccessToken.clear();
		AccessExpiresAt = 0;
		RefreshTokenValue.clear();
		ScopeList.clear();
		Player = User();
		bHasActivity = false;
		ActivityBody.clear();
		bPresenceInFlight = false;
		bClearAfterInFlight = false;
		bPresenceSentOnce = false;
		CancelFirstSend("Signed out before the activity was sent.");
		bEventsInFlight = false;
		Cursor = 0;
		EventsBackoffMs = 0;
		if (Cfg.store) Cfg.store->Remove(SessionKey());
		FlushWaiting(Reason.Ok() ? Error::Make(ErrorCode::NotSignedIn, "Signed out.") : Reason);
		if (bWas)
		{
			Log(LogLevel::Info, Change == SessionChange::SignedOut ? "Signed out of REAN." : "The REAN session ended: " + Reason.message);
			NotifySession(Change, Reason);
		}
	}

	void StartRefresh()
	{
		if (bRefreshing) return;
		if (RefreshTokenValue.empty())
		{
			FlushWaiting(Error::Make(ErrorCode::SessionEnded, "The REAN session expired. Sign in again."));
			return;
		}
		bRefreshing = true;
		const unsigned StartGen = Gen;
		OAuthPost("oauth/token", { { "grant_type", "refresh_token" }, { "refresh_token", RefreshTokenValue }, { "client_id", Cfg.clientId } },
			[this, StartGen](Reply& Rp) { OnRefreshAnswer(Rp, StartGen); });
	}

	void OnRefreshAnswer(Reply& Rp, unsigned StartGen)
	{
		if (StartGen != Gen) return;  // signed out meanwhile: that session is gone
		bRefreshing = false;
		Error E = Rp.error;
		if (E.Ok())
		{
			E = StoreTokens(Rp.body, Grant::Refresh, std::string());
			if (E.Ok())
			{
				RefreshBackoffMs = 0;
				RefreshRetryAt = 0;
				FlushWaiting(Error());
				return;
			}
		}
		if (E.code == ErrorCode::InvalidGrant)
		{
			// Expired, revoked, reused (chain revoked) or the player removed the app.
			Error End = E;
			End.code = ErrorCode::SessionEnded;
			End.message = "The REAN session ended (" + E.message + "). Sign in again.";
			DropSession(SessionChange::SessionEnded, End);
			return;
		}
		// Network, rate limit, server trouble: keep the session and try again later.
		RefreshBackoffMs = std::min<int64_t>(RefreshBackoffMs ? RefreshBackoffMs * 2 : 5000, 60000);
		RefreshRetryAt = Now() + std::max<int64_t>(RefreshBackoffMs, static_cast<int64_t>(Rp.retryAfterSeconds) * 1000);
		LastRefreshError = E;
		Log(LogLevel::Warning, "Could not refresh the REAN session: " + E.message);
		FlushWaiting(E);
	}

	// ── sign-in ──

	Error BeginSignIn(SignKind Kind)
	{
		if (!ConfigErr.Ok()) return ConfigErr;
		if (bSignedIn) return Error::Make(ErrorCode::Busy, "A player is already signed in. Sign out first.");
		if (Signing != SignKind::Idle) return Error::Make(ErrorCode::Busy, "A sign-in is already running.");
		Gen++;
		Signing = Kind;
		return Error();
	}

	void FailSignInLater(SignInCallback Done, const Error& E)
	{
		Defer([Done, E]() {
			if (Done) Done(E, User());
		});
	}

	void FinishSignIn(const Error& Result)
	{
		SignInCallback Done = std::move(SignInDone);
		SignInDone = nullptr;
		Signing = SignKind::Idle;
		OnDeviceCode = nullptr;
		DeviceCodeValue.clear();
		bDevicePolling = false;
		BrowserState.clear();
		BrowserNonce.clear();
		BrowserVerifier.clear();
		BrowserRedirect.clear();
		bBrowserExchanging = false;
		if (!Result.Ok())
		{
			Log(LogLevel::Info, std::string("Sign-in did not finish: ") + Result.message);
			FailSignInLater(std::move(Done), Result);
			return;
		}
		bSignedIn = true;
		LoadCursor();
		NextEvents = Now();
		EventsBackoffMs = 0;
		Log(LogLevel::Info, "Signed in with REAN as " + (Player.username.empty() ? Player.id : Player.username) + ".");
		NotifySession(SessionChange::SignedIn, Error());
		const User Copy = Player;
		Defer([Done, Copy]() {
			if (Done) Done(Error(), Copy);
		});
	}

	void TokenSignIn(const char* GrantType, const char* Field, const std::string& Value, Grant Kind, SignInCallback Done)
	{
		const Error E = BeginSignIn(SignKind::Token);
		if (!E.Ok()) return FailSignInLater(std::move(Done), E);
		SignInDone = std::move(Done);
		const unsigned StartGen = Gen;
		OAuthPost("oauth/token", { { "grant_type", GrantType }, { Field, Value }, { "client_id", Cfg.clientId } },
			[this, StartGen, Kind](Reply& Rp) {
				if (StartGen != Gen || Signing != SignKind::Token) return RevokeOrphan(Rp);  // cancelled meanwhile
				if (Rp.error.Ok())
				{
					FinishSignIn(StoreTokens(Rp.body, Kind, std::string()));
					return;
				}
				Error Err = Rp.error;
				if (Kind == Grant::Refresh && Err.code == ErrorCode::InvalidGrant)
				{
					if (Cfg.store) Cfg.store->Remove(SessionKey());
					Err.code = ErrorCode::SessionEnded;
					Err.message = "The saved REAN session ended (" + Err.message + "). Sign in again.";
				}
				FinishSignIn(Err);
			});
	}

	void StartDevice(DeviceCodeCallback OnCode, SignInCallback Done)
	{
		const Error E = BeginSignIn(SignKind::Device);
		if (!E.Ok()) return FailSignInLater(std::move(Done), E);
		SignInDone = std::move(Done);
		OnDeviceCode = std::move(OnCode);
		const unsigned StartGen = Gen;
		OAuthPost("oauth/device", { { "client_id", Cfg.clientId }, { "scope", Cfg.scopes } }, [this, StartGen](Reply& Rp) {
			if (StartGen != Gen || Signing != SignKind::Device) return;
			const std::string Code = Rp.body["device_code"].Str();
			if (!Rp.error.Ok() || Code.empty())
			{
				FinishSignIn(Rp.error.Ok() ? Error::Make(ErrorCode::Server, "REAN did not send a device code.") : Rp.error);
				return;
			}
			DeviceCode D;
			D.userCode = Rp.body["user_code"].Str();
			D.verificationUri = Rp.body["verification_uri"].Str();
			D.verificationUriComplete = Rp.body["verification_uri_complete"].Str(D.verificationUri);
			D.expiresInSeconds = static_cast<int>(std::max<int64_t>(30, std::min<int64_t>(Rp.body["expires_in"].Int(600), 3600)));
			D.intervalSeconds = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(Rp.body["interval"].Int(5), 60)));
			DeviceCodeValue = Code;
			DeviceIntervalMs = static_cast<int64_t>(D.intervalSeconds) * 1000;
			DeviceNextPoll = Now() + DeviceIntervalMs;
			DeviceDeadline = Now() + static_cast<int64_t>(D.expiresInSeconds) * 1000;
			Log(LogLevel::Info, "Device sign-in started: waiting for the player at " + D.verificationUri + ".");
			DeviceCodeCallback Cb = OnDeviceCode;
			if (Cb) Cb(D);
		});
	}

	void PollDevice()
	{
		bDevicePolling = true;
		const unsigned StartGen = Gen;
		OAuthPost("oauth/token", { { "grant_type", DeviceGrantType }, { "device_code", DeviceCodeValue }, { "client_id", Cfg.clientId } },
			[this, StartGen](Reply& Rp) {
				if (StartGen != Gen || Signing != SignKind::Device) return RevokeOrphan(Rp);
				bDevicePolling = false;
				if (Rp.error.Ok())
				{
					FinishSignIn(StoreTokens(Rp.body, Grant::Code, std::string()));
					return;
				}
				const std::string& A = Rp.error.apiCode;
				if (A == "slow_down")
				{
					DeviceIntervalMs = std::min<int64_t>(DeviceIntervalMs + 5000, MaxDeviceIntervalMs);
				}
				else if (A != "authorization_pending" && Rp.status != 0 && Rp.status < 500 && Rp.status != 429)
				{
					// access_denied, expired_token, invalid_grant, invalid_client: stop.
					FinishSignIn(Rp.error);
					return;
				}
				// Pending, or a passing network / server problem: ask again after the interval.
				DeviceNextPoll = Now() + std::max<int64_t>(DeviceIntervalMs, static_cast<int64_t>(Rp.retryAfterSeconds) * 1000);
			});
	}

	void TickSignIn()
	{
		const int64_t T = Now();
		if (Signing == SignKind::Device && !DeviceCodeValue.empty() && !bDevicePolling)
		{
			if (T >= DeviceDeadline) FinishSignIn(Error::Make(ErrorCode::Expired, "The sign-in code expired. Start sign-in again."));
			else if (T >= DeviceNextPoll) PollDevice();
		}
		else if (Signing == SignKind::Browser && !bBrowserExchanging && T >= BrowserDeadline)
		{
			Gen++;
			FinishSignIn(Error::Make(ErrorCode::Expired, "Browser sign-in timed out. Start sign-in again."));
		}
	}

	bool RandomToken(size_t Bytes, std::string& Out)
	{
		std::string Raw(Bytes, '\0');
		if (!Cfg.randomBytes(reinterpret_cast<uint8_t*>(&Raw[0]), Bytes)) return false;
		Out = Base64UrlEncode(Raw);
		return true;
	}

	// ── presence ──

	void SendPresenceDelete()
	{
		const unsigned StartGen = Gen;
		Api("DELETE", "v1/presence", std::string(), [this, StartGen](Reply& Rp) {
			if (StartGen == Gen && !Rp.error.Ok()) Log(LogLevel::Warning, "Could not clear REAN presence: " + Rp.error.message);
		});
	}

	/** Fire-and-forget DELETE with the current token (sign-out / exit: no refresh, no retry). */
	void SendPresenceDeleteNow()
	{
		if (AccessToken.empty() || Now() >= AccessExpiresAt) return;
		Transmit(NewRequest("DELETE", "v1/presence", std::string(), true, nullptr), AccessToken);
	}

	void TickPresence()
	{
		if (!bSignedIn || !bHasActivity || bPresenceInFlight || Now() < NextHeartbeat) return;
		bPresenceInFlight = true;
		bPresenceSentOnce = true;
		LastPresenceSent = Now();
		NextHeartbeat = LastPresenceSent + HeartbeatMs;
		const unsigned StartGen = Gen;
		const std::string SentBody = ActivityBody;
		Api("POST", "v1/presence", ActivityBody, [this, StartGen, SentBody](Reply& Rp) { OnPresenceAnswer(Rp, StartGen, SentBody); });
	}

	void CancelFirstSend(const char* Why)
	{
		if (!FirstSend) return;
		ResultCallback Old = std::move(FirstSend);
		FirstSend = nullptr;
		const Error E = Error::Make(ErrorCode::Cancelled, Why);
		Defer([Old, E]() { Old(E); });
	}

	void OnPresenceAnswer(Reply& Rp, unsigned StartGen, const std::string& SentBody)
	{
		if (StartGen != Gen) return;
		bPresenceInFlight = false;
		ResultCallback First;
		if (FirstSend && SentBody == ActivityBody)
		{
			First = std::move(FirstSend);  // this send was the activity the callback belongs to
			FirstSend = nullptr;
		}
		if (bClearAfterInFlight)
		{
			// ClearActivity was called while this update was on its way: clear after it.
			bClearAfterInFlight = false;
			SendPresenceDelete();
		}
		else if (!Rp.error.Ok())
		{
			switch (Rp.error.code)
			{
			case ErrorCode::MissingScope:
			case ErrorCode::InvalidRequest:
			case ErrorCode::Denied:
				Log(LogLevel::Error, "REAN refused the rich presence and it was cleared: " + Rp.error.message);
				bHasActivity = false;
				ActivityBody.clear();
				break;
			case ErrorCode::RateLimited:
				NextHeartbeat = Now() + std::max<int64_t>(HeartbeatMs, static_cast<int64_t>(Rp.retryAfterSeconds) * 1000);
				Log(LogLevel::Warning, "Rich presence is updated too often; slowing down.");
				break;
			default:
				NextHeartbeat = Now() + HeartbeatRetryMs;
				Log(LogLevel::Warning, "Could not send rich presence: " + Rp.error.message);
				break;
			}
		}
		if (First) First(Rp.error);
	}

	// ── events ──

	void LoadCursor()
	{
		Cursor = 0;
		std::string Saved;
		if (Cfg.store && !Player.id.empty() && Cfg.store->Load(CursorKey(), Saved) && !Saved.empty() && Saved.size() < 19 &&
			Saved.find_first_not_of("0123456789") == std::string::npos)
		{
			Cursor = std::stoll(Saved);
		}
	}

	bool IsStale(const Event& E) const
	{
		const int64_t ServerNow = ServerNowMs();
		int64_t T = 0;
		if (E.type == EventType::InviteReceived)
		{
			return ParseIso8601Ms(E.invite.expiresAt, T) && T <= ServerNow;  // nothing left to answer
		}
		return ParseIso8601Ms(E.createdAt, T) && T < ServerNow - static_cast<int64_t>(Cfg.maxEventAgeSeconds) * 1000;
	}

	void TickEvents()
	{
		if (!bSignedIn || !OnEvent || bEventsInFlight || Now() < NextEvents) return;
		bEventsInFlight = true;
		NextEvents = Now() + static_cast<int64_t>(Cfg.eventPollSeconds) * 1000;
		const unsigned StartGen = Gen;
		Api("GET", "v1/events?after=" + std::to_string(Cursor), std::string(), [this, StartGen](Reply& Rp) { OnEventsAnswer(Rp, StartGen); });
	}

	void OnEventsAnswer(Reply& Rp, unsigned StartGen)
	{
		if (StartGen != Gen) return;
		bEventsInFlight = false;
		if (!Rp.error.Ok())
		{
			if (Rp.error.code == ErrorCode::RateLimited)
			{
				EventsBackoffMs = std::min<int64_t>(std::max<int64_t>(30000, EventsBackoffMs * 2), 300000);
				NextEvents = Now() + std::max<int64_t>(EventsBackoffMs, static_cast<int64_t>(Rp.retryAfterSeconds) * 1000);
			}
			Log(LogLevel::Verbose, "Could not check REAN events: " + Rp.error.message);
			return;
		}
		EventsBackoffMs = 0;
		const Json& List = Rp.body["events"];
		std::vector<Event> Fresh;
		for (size_t I = 0; I < List.Size(); I++)
		{
			Event E = ParseEvent(List.At(I));
			if (E.id > Cursor && !IsStale(E)) Fresh.push_back(std::move(E));
		}
		int64_t NewCursor = Rp.body["cursor"].Int(Cursor);
		for (size_t I = 0; I < List.Size(); I++) NewCursor = std::max(NewCursor, List.At(I)["id"].Int(0));
		if (NewCursor > Cursor)
		{
			Cursor = NewCursor;
			if (Cfg.store) Cfg.store->Save(CursorKey(), std::to_string(Cursor));
		}
		if (List.Size() >= EventsPageSize) NextEvents = Now();  // more are waiting
		for (const Event& E : Fresh)
		{
			EventCallback Cb = OnEvent;  // copy: the handler may replace or clear it
			if (!Cb || StartGen != Gen) break;
			Cb(E);
		}
	}
};

// ─── Client ─────────────────────────────────────────────────────────────────

Client::Client(Config InConfig) : P(new Impl(std::move(InConfig)))
{
	P->Validate();
	if (!P->ConfigErr.Ok()) P->Log(LogLevel::Error, P->ConfigErr.message);
}

Client::~Client() = default;

const Error& Client::ConfigError() const { return P->ConfigErr; }
const Config& Client::GetConfig() const { return P->Cfg; }

void Client::Tick()
{
	P->RunDeferred();
	std::vector<std::pair<uint64_t, HttpResponse>> Done;
	{
		std::lock_guard<std::mutex> Lock(P->Box->Mutex);
		Done.swap(P->Box->Done);
	}
	for (auto& D : Done) P->HandleCompletion(D.first, D.second);
	if (P->ConfigErr.Ok())
	{
		P->TickSignIn();
		P->TickPresence();
		P->TickEvents();
	}
	P->RunDeferred();
}

void Client::SignInWithLauncher(const std::string& LauncherCode, SignInCallback Done)
{
	const std::string Code = TrimSpaces(LauncherCode);
	if (Code.empty() || Code.size() > 200 || HasControlOrSpace(Code))
	{
		return P->FailSignInLater(std::move(Done), Invalid("No valid REAN Launcher code (-ReanAuthCode)."));
	}
	{
		std::lock_guard<std::mutex> Lock(LauncherCodeMutex());
		if (UsedLauncherCodes().count(Code))
		{
			return P->FailSignInLater(std::move(Done), Error::Make(ErrorCode::InvalidGrant,
				"This REAN Launcher code was already used. Codes work once; restore the saved session or use device sign-in."));
		}
	}
	if (P->ConfigErr.Ok() && !P->bSignedIn && P->Signing == Impl::SignKind::Idle)
	{
		std::lock_guard<std::mutex> Lock(LauncherCodeMutex());
		UsedLauncherCodes().insert(Code);
	}
	P->TokenSignIn(LauncherGrantType, "code", Code, Impl::Grant::Code, std::move(Done));
}

void Client::SignInWithDevice(DeviceCodeCallback OnCode, SignInCallback Done)
{
	if (!OnCode) return P->FailSignInLater(std::move(Done), Invalid("Device sign-in needs a callback that shows the code."));
	P->StartDevice(std::move(OnCode), std::move(Done));
}

void Client::RestoreSession(SignInCallback Done)
{
	if (!P->ConfigErr.Ok()) return P->FailSignInLater(std::move(Done), P->ConfigErr);
	if (P->bSignedIn)
	{
		// Already signed in (e.g. a level reload asked again): report the current player.
		const User Copy = P->Player;
		P->Defer([Done, Copy]() {
			if (Done) Done(Error(), Copy);
		});
		return;
	}
	std::string Saved;
	if (!P->Cfg.saveSession || !P->Cfg.store || !P->Cfg.store->Load(P->SessionKey(), Saved) || TrimSpaces(Saved).empty())
	{
		return P->FailSignInLater(std::move(Done), Error::Make(ErrorCode::NoSavedSession, ""));
	}
	P->TokenSignIn("refresh_token", "refresh_token", TrimSpaces(Saved), Impl::Grant::Refresh, std::move(Done));
}

void Client::SignInWithRefreshToken(const std::string& Token, SignInCallback Done)
{
	const std::string T = TrimSpaces(Token);
	if (T.empty() || T.size() > 200 || HasControlOrSpace(T)) return P->FailSignInLater(std::move(Done), Invalid("No valid refresh token."));
	P->TokenSignIn("refresh_token", "refresh_token", T, Impl::Grant::Refresh, std::move(Done));
}

Error Client::BeginBrowserSignIn(const std::string& RedirectUri, std::string& OutAuthorizeUrl, SignInCallback Done)
{
	OutAuthorizeUrl.clear();
	if (!P->ConfigErr.Ok()) return P->ConfigErr;
	if (RedirectUri.empty() || RedirectUri.size() > 500 || HasControlOrSpace(RedirectUri)) return Invalid("Invalid redirect URI.");
	std::string State, Nonce, Verifier;
	if (!P->RandomToken(16, State) || !P->RandomToken(16, Nonce) || !P->RandomToken(32, Verifier))
	{
		return Error::Make(ErrorCode::NotSupported, "No secure random source for browser sign-in. Use device sign-in.");
	}
	const Error E = P->BeginSignIn(Impl::SignKind::Browser);
	if (!E.Ok()) return E;
	P->SignInDone = std::move(Done);
	P->BrowserState = State;
	P->BrowserNonce = Nonce;
	P->BrowserVerifier = Verifier;
	P->BrowserRedirect = RedirectUri;
	P->BrowserDeadline = P->Now() + BrowserTimeoutMs;
	P->bBrowserExchanging = false;
	const std::string& Base = P->Cfg.authorizeUrl;
	OutAuthorizeUrl = Base + (Base.find('?') == std::string::npos ? "?" : "&") +
		FormEncode({ { "client_id", P->Cfg.clientId },
		             { "redirect_uri", RedirectUri },
		             { "response_type", "code" },
		             { "scope", P->Cfg.scopes },
		             { "state", State },
		             { "code_challenge", PkceChallenge(Verifier) },
		             { "code_challenge_method", "S256" },
		             { "nonce", Nonce } });
	return Error();
}

Error Client::CompleteBrowserSignIn(const std::string& CallbackUrlOrQuery)
{
	if (P->Signing != Impl::SignKind::Browser) return Invalid("No browser sign-in is waiting.");
	if (P->bBrowserExchanging) return Error::Make(ErrorCode::Busy, "The sign-in is already being finished.");
	const KeyValues Params = ParseQuery(CallbackUrlOrQuery);
	std::string State;
	if (!FindParam(Params, "state", State) || State != P->BrowserState)
	{
		// Not ours (or forged): ignore it and keep waiting for the real answer.
		return Invalid("The answer does not belong to this sign-in (state does not match).");
	}
	std::string ErrorText;
	if (FindParam(Params, "error", ErrorText))
	{
		std::string Description;
		FindParam(Params, "error_description", Description);
		Error E;
		E.apiCode = ErrorText;
		E.code = (ErrorText == "access_denied" || ErrorText == "consent_required") ? ErrorCode::Denied
		         : ErrorText == "invalid_scope" ? ErrorCode::MissingScope
		                                        : ErrorCode::InvalidRequest;
		E.message = Description.empty() ? std::string(DefaultMessage(E.code)) : Description;
		P->Gen++;
		P->FinishSignIn(E);
		return Error();
	}
	std::string Code;
	if (!FindParam(Params, "code", Code) || Code.empty() || Code.size() > 200) return Invalid("The answer has no authorization code.");
	P->bBrowserExchanging = true;
	const unsigned StartGen = P->Gen;
	const std::string Nonce = P->BrowserNonce;
	Impl* Self = P.get();
	P->OAuthPost("oauth/token",
		{ { "grant_type", "authorization_code" },
		  { "code", Code },
		  { "redirect_uri", P->BrowserRedirect },
		  { "code_verifier", P->BrowserVerifier },
		  { "client_id", P->Cfg.clientId } },
		[Self, StartGen, Nonce](Reply& Rp) {
			if (StartGen != Self->Gen || Self->Signing != Impl::SignKind::Browser) return Self->RevokeOrphan(Rp);
			Self->FinishSignIn(Rp.error.Ok() ? Self->StoreTokens(Rp.body, Impl::Grant::Code, Nonce) : Rp.error);
		});
	return Error();
}

void Client::CancelSignIn()
{
	if (P->Signing == Impl::SignKind::Idle) return;
	P->Gen++;
	P->FinishSignIn(Error::Make(ErrorCode::Cancelled, "Sign-in was cancelled."));
}

void Client::SignOut()
{
	CancelSignIn();
	if (!P->ConfigErr.Ok()) return;
	std::string Token = P->RefreshTokenValue;
	if (Token.empty() && !P->bSignedIn && P->Cfg.store) P->Cfg.store->Load(P->SessionKey(), Token);  // a session never restored
	if (P->bSignedIn && P->bHasActivity) P->SendPresenceDeleteNow();
	if (!Token.empty())
	{
		// RFC 7009: ends the refresh token's whole rotation chain. Always answers 200.
		P->OAuthPost("oauth/revoke", { { "token", Token }, { "client_id", P->Cfg.clientId } }, nullptr);
	}
	if (P->Cfg.store) P->Cfg.store->Remove(P->SessionKey());
	if (P->bSignedIn) P->DropSession(SessionChange::SignedOut, Error());
}

void Client::Shutdown()
{
	if (P->bSignedIn && P->bHasActivity) P->SendPresenceDeleteNow();
	P->bHasActivity = false;
	P->ActivityBody.clear();
	P->CancelFirstSend("The game is shutting down.");
}

bool Client::IsSignedIn() const { return P->bSignedIn; }
bool Client::IsSigningIn() const { return P->Signing != Impl::SignKind::Idle; }
const User& Client::CurrentUser() const { return P->Player; }
const std::string& Client::GrantedScopes() const { return P->ScopeList; }
bool Client::HasScope(const std::string& Scope) const { return ContainsScope(P->ScopeList, Scope); }
const std::string& Client::RefreshToken() const { return P->RefreshTokenValue; }
void Client::SetSessionCallback(SessionCallback Callback) { P->OnSession = std::move(Callback); }

void Client::GetMe(UserCallback Done)
{
	P->Api("GET", "v1/me", std::string(), [Done](Reply& Rp) {
		if (!Done) return;
		if (!Rp.error.Ok()) return Done(Rp.error, User());
		Done(Error(), ParseUser(Rp.body));
	});
}

void Client::GetFriends(FriendsCallback Done)
{
	P->Api("GET", "v1/friends", std::string(), [Done](Reply& Rp) {
		if (!Done) return;
		std::vector<Friend> Out;
		if (!Rp.error.Ok()) return Done(Rp.error, Out);
		const Json& List = Rp.body["friends"];
		for (size_t I = 0; I < List.Size(); I++)
		{
			const Json& J = List.At(I);
			Friend F;
			F.user = ParseUser(J);
			F.friendsSince = J["friendsSince"].Str();
			if (J.Has("presence"))
			{
				F.hasPresence = true;
				F.presence = ParsePresence(J["presence"]);
			}
			Out.push_back(std::move(F));
		}
		Done(Error(), Out);
	});
}

void Client::GetPresence(const std::vector<std::string>& PlayerIds, PresenceCallback Done)
{
	auto Fail = [this, &Done](const Error& E) {
		P->Defer([Done, E]() {
			if (Done) Done(E, std::vector<PlayerPresence>());
		});
	};
	if (PlayerIds.size() > MaxPresenceIds) return Fail(Invalid("Ask for at most 100 players at a time."));
	std::string Ids;
	for (const std::string& Id : PlayerIds)
	{
		if (!IsUuid(Id)) return Fail(Invalid("\"" + Id.substr(0, 40) + "\" is not a REAN player id."));
		if (!Ids.empty()) Ids.push_back(',');
		Ids += Id;
	}
	if (PlayerIds.empty() && P->bSignedIn) return Fail(Error());
	P->Api("GET", "v1/presence?ids=" + UrlEncode(Ids), std::string(), [Done](Reply& Rp) {
		if (!Done) return;
		std::vector<PlayerPresence> Out;
		if (!Rp.error.Ok()) return Done(Rp.error, Out);
		const Json& List = Rp.body["presence"];
		for (size_t I = 0; I < List.Size(); I++)
		{
			PlayerPresence Item;
			Item.playerId = List.At(I)["playerId"].Str();
			Item.presence = ParsePresence(List.At(I));
			Out.push_back(std::move(Item));
		}
		Done(Error(), Out);
	});
}

Error Client::SetActivity(const Activity& A, ResultCallback OnFirstSend)
{
	if (!P->ConfigErr.Ok()) return P->ConfigErr;
	if (!P->bSignedIn) return Error::Make(ErrorCode::NotSignedIn, "");
	if (!A.status.empty() && A.status != "in_game" && A.status != "online" && A.status != "away" && A.status != "dnd")
	{
		return Invalid("Status must be in_game, online, away or dnd.");
	}
	if (Utf16Length(A.details) > MaxActivityText) return Invalid("Details is longer than 128 characters.");
	if (Utf16Length(A.state) > MaxActivityText) return Invalid("State is longer than 128 characters.");
	if (Utf16Length(A.partyId) > MaxActivityText) return Invalid("PartyId is longer than 128 characters.");
	if (A.partySize < -1 || A.partySize > 1000 || A.partyMax < -1 || A.partyMax > 1000)
	{
		return Invalid("PartySize and PartyMax must be 0 to 1000 (or -1 for not set).");
	}
	if (A.partySize >= 0 && A.partyMax >= 0 && A.partySize > A.partyMax) return Invalid("PartySize cannot be larger than PartyMax.");
	if (A.startedAt < 0) return Invalid("StartedAt must be unix seconds (0 = not set).");
	if (A.startedAt > 100000000000LL) return Invalid("StartedAt looks like milliseconds; REAN wants unix seconds.");
	if (A.startedAt > 0 && A.startedAt * 1000 > P->Cfg.unixMs() + 60000) return Invalid("StartedAt is in the future.");

	JsonObjectWriter W;
	W.Str("status", A.status.empty() ? std::string("in_game") : A.status);
	if (!A.details.empty()) W.Str("details", A.details);
	if (!A.state.empty()) W.Str("state", A.state);
	if (!A.partyId.empty()) W.Str("partyId", A.partyId);
	if (A.partySize >= 0) W.Int("partySize", A.partySize);
	if (A.partyMax >= 0) W.Int("partyMax", A.partyMax);
	W.Bool("joinable", A.joinable);
	if (A.startedAt > 0) W.Int("startedAt", A.startedAt);
	const std::string Body = W.Finish();

	P->CancelFirstSend("Replaced by a newer activity.");
	P->FirstSend = std::move(OnFirstSend);
	const bool bChanged = !P->bHasActivity || Body != P->ActivityBody || P->bClearAfterInFlight;
	P->bHasActivity = true;
	P->ActivityBody = Body;
	P->bClearAfterInFlight = false;
	if (bChanged)
	{
		const int64_t Earliest = P->bPresenceSentOnce ? P->LastPresenceSent + PresenceMinGapMs : P->Now();
		P->NextHeartbeat = std::max(P->Now(), Earliest);
	}
	return Error();
}

void Client::ClearActivity()
{
	if (!P->bHasActivity) return;
	P->bHasActivity = false;
	P->ActivityBody.clear();
	P->CancelFirstSend("The activity was cleared.");
	if (!P->bSignedIn) return;
	if (P->bPresenceInFlight)
	{
		P->bClearAfterInFlight = true;
		return;
	}
	if (P->bPresenceSentOnce) P->SendPresenceDelete();
}

bool Client::HasActivity() const { return P->bHasActivity; }

void Client::GetInvites(InvitesCallback Done)
{
	P->Api("GET", "v1/invites", std::string(), [Done](Reply& Rp) {
		if (!Done) return;
		std::vector<Invite> Out;
		if (!Rp.error.Ok()) return Done(Rp.error, Out);
		const Json& List = Rp.body["invites"];
		for (size_t I = 0; I < List.Size(); I++) Out.push_back(ParseInvite(List.At(I)));
		Done(Error(), Out);
	});
}

void Client::SendInvite(const std::string& FriendId, const std::string& JoinSecret, const std::string& Message, SentInviteCallback Done)
{
	auto Fail = [this, &Done](const Error& E) {
		P->Defer([Done, E]() {
			if (Done) Done(E, SentInvite());
		});
	};
	if (!IsUuid(FriendId)) return Fail(Invalid("FriendId must be a REAN player id (a UUID, e.g. from GetFriends)."));
	if (P->bSignedIn && FriendId == P->Player.id) return Fail(Invalid("Players cannot invite themselves."));
	if (Utf16Length(JoinSecret) > MaxJoinSecret) return Fail(Invalid("JoinSecret is longer than 256 characters."));
	if (Utf16Length(Message) > MaxInviteMessage) return Fail(Invalid("Message is longer than 140 characters."));
	JsonObjectWriter W;
	W.Str("to", FriendId);
	if (!JoinSecret.empty()) W.Str("joinSecret", JoinSecret);
	if (!Message.empty()) W.Str("message", Message);
	P->Api("POST", "v1/invites", W.Finish(), [Done](Reply& Rp) {
		if (!Done) return;
		SentInvite S;
		if (!Rp.error.Ok()) return Done(Rp.error, S);
		const Json& J = Rp.body["invite"];
		S.id = J["id"].Str();
		S.to = J["to"].Str();
		S.expiresAt = J["expiresAt"].Str();
		Done(Error(), S);
	});
}

void Client::RespondToInvite(const std::string& InviteId, bool bAccept, JoinCallback Done)
{
	if (InviteId.empty() || InviteId.size() > 64 || HasControlOrSpace(InviteId))
	{
		const Error E = Invalid("Invalid invite id.");
		P->Defer([Done, E]() {
			if (Done) Done(E, std::string());
		});
		return;
	}
	JsonObjectWriter W;
	W.Str("id", InviteId);
	W.Bool("accept", bAccept);
	P->Api("POST", "v1/invites/respond", W.Finish(), [Done](Reply& Rp) {
		if (!Done) return;
		if (!Rp.error.Ok()) return Done(Rp.error, std::string());
		Done(Error(), Rp.body["joinSecret"].Str());
	});
}

void Client::SetEventCallback(EventCallback Callback)
{
	const bool bWasSet = static_cast<bool>(P->OnEvent);
	P->OnEvent = std::move(Callback);
	if (P->OnEvent && !bWasSet) P->NextEvents = P->Now();
}

void Client::SetEventPollSeconds(int Seconds)
{
	P->Cfg.eventPollSeconds = std::max(10, std::min(300, Seconds));
}

} // namespace rean
