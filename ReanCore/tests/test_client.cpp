// Copyright (c) REAN Studios. MIT License.
//
// rean::Client against a fake transport: sign-in flows, token rules, the
// generation counter, presence heartbeat, events and error mapping.
#include "doctest.h"

#include "fakes.h"

using namespace reantest;
using rean::ErrorCode;

namespace
{
struct SignInResult
{
	int calls = 0;
	rean::Error error;
	rean::User user;
	rean::SignInCallback Callback()
	{
		return [this](const rean::Error& E, const rean::User& U) {
			calls++;
			error = E;
			user = U;
		};
	}
};

struct SessionLog
{
	std::vector<rean::SessionChange> changes;
	std::vector<rean::Error> reasons;
	rean::SessionCallback Callback()
	{
		return [this](rean::SessionChange C, const rean::Error& E) {
			changes.push_back(C);
			reasons.push_back(E);
		};
	}
};

const char* DeviceAnswer = R"({"device_code":"rdc_secret","user_code":"BCDF-GHJK","verification_uri":"https://reanstudios.com/activate","verification_uri_complete":"https://reanstudios.com/activate?code=BCDF-GHJK","expires_in":600,"interval":5})";
} // namespace

TEST_CASE("config: a bad client id fails every call, asynchronously")
{
	Harness H("rean_NOT_VALID");
	CHECK(H->ConfigError().code == ErrorCode::InvalidConfig);
	SignInResult R;
	H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
	CHECK(R.calls == 0);  // never inside the call
	H.Tick();
	CHECK(R.calls == 1);
	CHECK(R.error.code == ErrorCode::InvalidConfig);
	CHECK(H.transport->sent.empty());
}

TEST_CASE("config: ApiBase must be https and loses its trailing slash")
{
	rean::Config C;
	C.clientId = ClientId;
	C.transport = std::make_shared<FakeTransport>();
	C.apiBase = "http://api.reanstudios.com";
	CHECK(rean::Client(C).ConfigError().code == ErrorCode::InvalidConfig);
	C.apiBase = "https://rean-client-vyli.vercel.app/";
	rean::Client Ok(C);
	CHECK(Ok.ConfigError().Ok());
	CHECK(Ok.GetConfig().apiBase == "https://rean-client-vyli.vercel.app");
	C.transport = nullptr;
	CHECK(rean::Client(C).ConfigError().code == ErrorCode::InvalidConfig);
}

TEST_CASE("device: full flow with authorization_pending and slow_down")
{
	Harness H;
	SessionLog Sessions;
	H->SetSessionCallback(Sessions.Callback());
	SignInResult R;
	rean::DeviceCode Shown;
	int CodeCalls = 0;
	H->SignInWithDevice([&](const rean::DeviceCode& D) { Shown = D; CodeCalls++; }, R.Callback());
	CHECK(H->IsSigningIn());
	REQUIRE(H.transport->sent.size() == 1);
	const Sent& Start = H.transport->sent[0];
	CHECK(Start.request.method == "POST");
	CHECK(Start.request.url == "https://api.reanstudios.com/api/oauth/device");
	CHECK(Start.HeaderValue("Content-Type") == "application/x-www-form-urlencoded");
	CHECK(Start.HeaderValue("Authorization").empty());
	CHECK(Start.Form("client_id") == ClientId);
	CHECK(Start.Form("scope") == rean::DefaultScopes);
	CHECK(Start.Form("client_secret").empty());

	H.transport->Answer("oauth/device", 200, DeviceAnswer);
	H.Tick();
	CHECK(CodeCalls == 1);
	CHECK(Shown.userCode == "BCDF-GHJK");
	CHECK(Shown.verificationUri == "https://reanstudios.com/activate");
	CHECK(Shown.verificationUriComplete == "https://reanstudios.com/activate?code=BCDF-GHJK");
	CHECK(Shown.expiresInSeconds == 600);

	H.Advance(4999);
	CHECK(H.transport->CountSent("oauth/token") == 0);  // waits the interval
	H.Advance(1);
	REQUIRE(H.transport->CountSent("oauth/token") == 1);
	const Sent* Poll = H.transport->Last("oauth/token");
	CHECK(Poll->Form("grant_type") == "urn:ietf:params:oauth:grant-type:device_code");
	CHECK(Poll->Form("device_code") == "rdc_secret");
	CHECK(Poll->Form("client_id") == ClientId);

	H.transport->Answer("oauth/token", 400, OAuthError("authorization_pending", "Waiting for the player to approve."));
	H.Tick();
	H.Advance(5000);
	CHECK(H.transport->CountSent("oauth/token") == 2);  // one poll at a time, every interval

	H.transport->Answer("oauth/token", 400, OAuthError("slow_down", "Polling too fast."));
	H.Tick();
	H.Advance(5000);
	CHECK(H.transport->CountSent("oauth/token") == 2);  // interval is now 10 s
	H.Advance(5000);
	CHECK(H.transport->CountSent("oauth/token") == 3);

	H.transport->Answer("oauth/token", 0, "");  // network blip: keep polling
	H.Tick();
	H.Advance(10000);
	CHECK(H.transport->CountSent("oauth/token") == 4);

	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_device", Alice, ",\"picture\":\"https://cdn/a.png\""));
	CHECK(R.calls == 0);
	H.Tick();
	CHECK(R.calls == 1);
	CHECK(R.error.Ok());
	CHECK(R.user.id == Alice);
	CHECK(R.user.username == "alice");
	CHECK(R.user.displayName == "Alice");
	CHECK(R.user.avatarUrl == "https://cdn/a.png");
	CHECK(H->IsSignedIn());
	CHECK_FALSE(H->IsSigningIn());
	CHECK(H->HasScope("presence.write"));
	CHECK_FALSE(H->HasScope("email"));
	CHECK(H.Saved() == "rrt_device");
	REQUIRE(Sessions.changes.size() == 1);
	CHECK(Sessions.changes[0] == rean::SessionChange::SignedIn);
	for (const std::string& Line : H.logs)
	{
		CHECK(Line.find("rrt_") == std::string::npos);  // tokens never reach the log
		CHECK(Line.find("rdc_") == std::string::npos);
		CHECK(Line.find("BCDF-GHJK") == std::string::npos);
	}
}

TEST_CASE("device: denied, expired on REAN, and expired locally")
{
	{
		Harness H;
		SignInResult R;
		H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
		H.transport->Answer("oauth/device", 200, DeviceAnswer);
		H.Tick();
		H.Advance(5000);
		H.transport->Answer("oauth/token", 400, OAuthError("access_denied", "The player declined."));
		H.Tick();
		CHECK(R.calls == 1);
		CHECK(R.error.code == ErrorCode::Denied);
		CHECK(R.error.message == "The player declined.");
		CHECK_FALSE(H->IsSigningIn());
	}
	{
		Harness H;
		SignInResult R;
		H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
		H.transport->Answer("oauth/device", 200, DeviceAnswer);
		H.Tick();
		H.Advance(5000);
		H.transport->Answer("oauth/token", 400, OAuthError("expired_token", "The code expired. Start again."));
		H.Tick();
		CHECK(R.error.code == ErrorCode::Expired);
	}
	{
		Harness H;
		SignInResult R;
		H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
		H.transport->Answer("oauth/device", 200, DeviceAnswer);
		H.Tick();
		for (int I = 0; I < 130 && R.calls == 0; I++)
		{
			H.Advance(5000);
			H.transport->Answer("oauth/token", 400, OAuthError("authorization_pending"));
			H.Tick();
		}
		CHECK(R.calls == 1);
		CHECK(R.error.code == ErrorCode::Expired);
	}
	{
		Harness H;
		SignInResult R;
		H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
		H.transport->Answer("oauth/device", 401, OAuthError("invalid_client", "Unknown client."));
		H.Tick();
		CHECK(R.error.code == ErrorCode::InvalidClient);
		CHECK(R.error.httpStatus == 401);
	}
	{
		Harness H;
		SignInResult R;
		H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
		H.transport->Answer("oauth/device", 429, OAuthError("slow_down", "Too many attempts."));
		H.Tick();
		CHECK(R.error.code == ErrorCode::RateLimited);
	}
}

TEST_CASE("generation: a late answer after cancel changes nothing and its session is revoked")
{
	Harness H;
	SignInResult R;
	H->SignInWithDevice([](const rean::DeviceCode&) {}, R.Callback());
	H.transport->Answer("oauth/device", 200, DeviceAnswer);
	H.Tick();
	H.Advance(5000);
	REQUIRE(H.transport->Pending("oauth/token").size() == 1);
	H->CancelSignIn();
	H.Tick();
	CHECK(R.calls == 1);
	CHECK(R.error.code == ErrorCode::Cancelled);
	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_late"));
	H.Tick(3);
	CHECK(R.calls == 1);
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.Saved().empty());
	const Sent* Revoke = H.transport->Last("oauth/revoke");
	REQUIRE(Revoke);
	CHECK(Revoke->Form("token") == "rrt_late");
	CHECK(Revoke->Form("client_id") == ClientId);
	H.Advance(60000);
	CHECK(H.transport->CountSent("oauth/token") == 1);  // no more polling
}

TEST_CASE("launcher: exchanges the code once, never twice")
{
	Harness H;
	SignInResult R;
	H->SignInWithLauncher("rlc_once_only", R.Callback());
	const Sent* T = H.transport->Last("oauth/token");
	REQUIRE(T);
	CHECK(T->Form("grant_type") == "urn:rean:params:oauth:grant-type:launcher_code");
	CHECK(T->Form("code") == "rlc_once_only");
	H.transport->Answer("oauth/token", 400, OAuthError("invalid_grant", "The code has expired."));
	H.Tick();
	CHECK(R.error.code == ErrorCode::InvalidGrant);

	Harness Other;  // another client in the same process (e.g. a second PIE instance)
	SignInResult R2;
	Other->SignInWithLauncher("rlc_once_only", R2.Callback());
	Other.Tick();
	CHECK(R2.error.code == ErrorCode::InvalidGrant);
	CHECK(Other.transport->sent.empty());

	SignInResult R3;
	H->SignInWithLauncher("rlc has spaces", R3.Callback());
	H.Tick();
	CHECK(R3.error.code == ErrorCode::InvalidArgument);
}

TEST_CASE("busy: one sign-in at a time, and not while signed in")
{
	Harness H;
	SignInResult A, B;
	H->SignInWithDevice([](const rean::DeviceCode&) {}, A.Callback());
	H->SignInWithLauncher("rlc_busy_1", B.Callback());
	H.Tick();
	CHECK(B.error.code == ErrorCode::Busy);
	CHECK(A.calls == 0);
	H->CancelSignIn();
	H.SignIn();
	REQUIRE(H->IsSignedIn());
	SignInResult C;
	H->SignInWithDevice([](const rean::DeviceCode&) {}, C.Callback());
	H.Tick();
	CHECK(C.error.code == ErrorCode::Busy);
	SignInResult D;
	H->RestoreSession(D.Callback());  // already signed in: reports the player
	H.Tick();
	CHECK(D.error.Ok());
	CHECK(D.user.id == Alice);
}

TEST_CASE("restore: saved session, rotation, invalid_grant and network failure")
{
	{
		Harness H;
		SignInResult R;
		H->RestoreSession(R.Callback());
		H.Tick();
		CHECK(R.error.code == ErrorCode::NoSavedSession);
		CHECK(H.transport->sent.empty());
	}
	{
		Harness H;
		H.store->Save(H.SessionKey(), "rrt_saved");
		SignInResult R;
		H->RestoreSession(R.Callback());
		const Sent* T = H.transport->Last("oauth/token");
		REQUIRE(T);
		CHECK(T->Form("grant_type") == "refresh_token");
		CHECK(T->Form("refresh_token") == "rrt_saved");
		H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_rotated"));
		H.Tick();
		CHECK(R.error.Ok());
		CHECK(R.user.username == "alice");
		CHECK(H.Saved() == "rrt_rotated");  // the new token replaces the spent one
		CHECK(H->RefreshToken() == "rrt_rotated");
	}
	{
		Harness H;
		H.store->Save(H.SessionKey(), "rrt_revoked");
		SignInResult R;
		H->RestoreSession(R.Callback());
		H.transport->Answer("oauth/token", 400, OAuthError("invalid_grant", "The refresh token was already used. Sign in again."));
		H.Tick();
		CHECK(R.error.code == ErrorCode::SessionEnded);
		CHECK(H.Saved().empty());
	}
	{
		Harness H;
		H.store->Save(H.SessionKey(), "rrt_keep");
		SignInResult R;
		H->RestoreSession(R.Callback());
		H.transport->Answer("oauth/token", 0, "");
		H.Tick();
		CHECK(R.error.code == ErrorCode::Network);
		CHECK(H.Saved() == "rrt_keep");  // offline at launch: keep the session for later
	}
	{
		Harness H;
		H.store->Save(H.SessionKey(), "rrt_busy");
		SignInResult R;
		H->RestoreSession(R.Callback());
		H.transport->Answer("oauth/token", 429, OAuthError("slow_down", "Too many attempts. Please wait a moment and try again."));
		H.Tick();
		CHECK(R.error.code == ErrorCode::RateLimited);
		CHECK(H.Saved() == "rrt_busy");
	}
}

TEST_CASE("refresh: a minute before expiry, one at a time, then the waiting calls go out")
{
	Harness H;
	H.SignIn("rrt_1");
	H.monotonic += (3600 - 59) * 1000;  // less than a minute left
	int Done = 0;
	std::vector<rean::Friend> Got;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>& F) { Done++; CHECK(E.Ok()); Got = F; });
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { Done++; CHECK(E.Ok()); });
	H.Tick();
	CHECK(H.transport->CountSent("v1/friends") == 0);
	CHECK(H.transport->Pending("oauth/token").size() == 1);  // never refresh in parallel
	CHECK(H.transport->Last("oauth/token")->Form("refresh_token") == "rrt_1");
	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_2"));
	H.Tick();
	CHECK(H.Saved() == "rrt_2");
	REQUIRE(H.transport->Pending("v1/friends").size() == 2);
	const std::string Bearer = H.transport->Pending("v1/friends")[0]->HeaderValue("Authorization");
	CHECK(Bearer.compare(0, 7, "Bearer ") == 0);
	H.transport->Answer("v1/friends", 200, R"({"friends":[]})");
	H.transport->Answer("v1/friends", 200, R"({"friends":[]})");
	H.Tick();
	CHECK(Done == 2);
}

TEST_CASE("401: refresh once and resend; a second 401 ends the session")
{
	Harness H;
	SessionLog Sessions;
	H->SetSessionCallback(Sessions.Callback());
	H.SignIn("rrt_a");
	rean::Error Result;
	int Calls = 0;
	H->GetMe([&](const rean::Error& E, const rean::User&) { Calls++; Result = E; });
	H.transport->Answer("v1/me", 401, R"({"error":"invalid_token","error_description":"The access token is invalid or expired."})");
	H.Tick();
	CHECK(Calls == 0);
	REQUIRE(H.transport->Pending("oauth/token").size() == 1);
	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_b"));
	H.Tick();
	REQUIRE(H.transport->Pending("v1/me").size() == 1);  // resent once
	H.transport->Answer("v1/me", 401, R"({"error":"invalid_token","error_description":"The player removed this app."})");
	H.Tick();
	CHECK(Calls == 1);
	CHECK(Result.code == ErrorCode::SessionEnded);
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.Saved().empty());
	CHECK(H.transport->CountSent("oauth/token") == 2);  // no endless refresh loop
	REQUIRE(Sessions.changes.size() == 2);
	CHECK(Sessions.changes[1] == rean::SessionChange::SessionEnded);
}

TEST_CASE("refresh: invalid_grant (reuse revoked the chain) signs the player out")
{
	Harness H;
	H.SignIn("rrt_x");
	H.monotonic += 3600 * 1000;
	rean::Error A, B;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { A = E; });
	H->GetInvites([&](const rean::Error& E, const std::vector<rean::Invite>&) { B = E; });
	H.Tick();
	H.transport->Answer("oauth/token", 400, OAuthError("invalid_grant", "The refresh token was already used. Sign in again."));
	H.Tick();
	CHECK(A.code == ErrorCode::SessionEnded);
	CHECK(B.code == ErrorCode::SessionEnded);
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.Saved().empty());
}

TEST_CASE("refresh: a network failure keeps the session and backs off")
{
	Harness H;
	H.SignIn("rrt_net");
	H.monotonic += 3600 * 1000;
	rean::Error A;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { A = E; });
	H.Tick();
	H.transport->Answer("oauth/token", 0, "");
	H.Tick();
	CHECK(A.code == ErrorCode::Network);
	CHECK(H->IsSignedIn());
	CHECK(H.Saved() == "rrt_net");
	rean::Error B;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { B = E; });
	H.Tick();
	CHECK(B.code == ErrorCode::Network);  // within the back-off: fails fast
	CHECK(H.transport->CountSent("oauth/token") == 2);
	H.Advance(5000);
	H->GetFriends([](const rean::Error&, const std::vector<rean::Friend>&) {});
	H.Tick();
	CHECK(H.transport->CountSent("oauth/token") == 3);
	CHECK(H.transport->Last("oauth/token")->Form("refresh_token") == "rrt_net");
}

TEST_CASE("sign-out: clears presence, revokes, forgets; late answers change nothing")
{
	Harness H;
	SessionLog Sessions;
	H->SetSessionCallback(Sessions.Callback());
	H.SignIn("rrt_out");
	rean::Activity A;
	A.details = "Lobby";
	REQUIRE(H->SetActivity(A).Ok());
	H.Tick();
	H.transport->Answer("v1/presence", 200, R"({"ok":true,"expiresIn":120})");
	H.Tick();
	rean::Error Late;
	int LateCalls = 0;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { LateCalls++; Late = E; });
	H->SignOut();
	CHECK_FALSE(H->IsSignedIn());
	const Sent* Del = H.transport->Last("v1/presence");
	REQUIRE(Del);
	CHECK(Del->request.method == "DELETE");
	CHECK_FALSE(Del->HeaderValue("Authorization").empty());
	const Sent* Rev = H.transport->Last("oauth/revoke");
	REQUIRE(Rev);
	CHECK(Rev->Form("token") == "rrt_out");
	CHECK(H.Saved().empty());
	H.transport->Answer("v1/friends", 200, R"({"friends":[{"id":"x","username":"x"}]})");
	H.Tick();
	CHECK(LateCalls == 1);
	CHECK(Late.code == ErrorCode::NotSignedIn);  // no data after sign-out
	REQUIRE(Sessions.changes.size() == 2);
	CHECK(Sessions.changes[1] == rean::SessionChange::SignedOut);
	H.Advance(120000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 1);  // heartbeat stopped
}

TEST_CASE("sign-out during a refresh: the late refresh answer is ignored")
{
	Harness H;
	H.SignIn("rrt_r1");
	H.monotonic += 3600 * 1000;
	rean::Error A;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { A = E; });
	H.Tick();
	REQUIRE(H.transport->Pending("oauth/token").size() == 1);
	H->SignOut();
	H.Tick();
	CHECK(A.code == ErrorCode::NotSignedIn);
	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_r2"));
	H.Tick();
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.Saved().empty());

	// A new sign-in afterwards works normally.
	H.SignIn("rrt_new");
	CHECK(H->IsSignedIn());
	CHECK(H.Saved() == "rrt_new");
}

TEST_CASE("sign-out while signed out forgets and revokes a saved session")
{
	Harness H;
	H.store->Save(H.SessionKey(), "rrt_never_restored");
	H->SignOut();
	CHECK(H.Saved().empty());
	REQUIRE(H.transport->Last("oauth/revoke"));
	CHECK(H.transport->Last("oauth/revoke")->Form("token") == "rrt_never_restored");
}

TEST_CASE("presence: validation, body, heartbeat every 60 s, clear")
{
	Harness H;
	rean::Activity A;
	CHECK(H->SetActivity(A).code == ErrorCode::NotSignedIn);
	H.SignIn();

	A.details = std::string(129, 'x');
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.details = "Ranked";
	A.partySize = 6;
	A.partyMax = 5;
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.partySize = 1001;
	A.partyMax = -1;
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.partySize = 2;
	A.partyMax = 5;
	A.status = "playing";
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.status.clear();
	A.startedAt = StartUnixMs;  // milliseconds by mistake
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.startedAt = StartUnixMs / 1000 + 3600;  // in the future
	CHECK(H->SetActivity(A).code == ErrorCode::InvalidArgument);
	A.startedAt = StartUnixMs / 1000 - 30;
	A.state = "Round 3";
	A.partyId = "lobby-7";
	A.joinable = true;
	CHECK(H.transport->CountSent("v1/presence") == 0);

	rean::Error First = rean::Error::Make(ErrorCode::Server, "not called");
	int FirstCalls = 0;
	REQUIRE(H->SetActivity(A, [&](const rean::Error& E) { FirstCalls++; First = E; }).Ok());
	CHECK(H->HasActivity());
	H.Tick();
	REQUIRE(H.transport->Pending("v1/presence").size() == 1);
	const Sent* Post = H.transport->Pending("v1/presence")[0];
	CHECK(Post->request.method == "POST");
	CHECK(Post->HeaderValue("Content-Type") == "application/json");
	const rean::Json Body = Post->JsonBody();
	CHECK(Body["status"].Str() == "in_game");
	CHECK(Body["details"].Str() == "Ranked");
	CHECK(Body["state"].Str() == "Round 3");
	CHECK(Body["partyId"].Str() == "lobby-7");
	CHECK(Body["partySize"].Int(0) == 2);
	CHECK(Body["partyMax"].Int(0) == 5);
	CHECK(Body["joinable"].Bool(false));
	CHECK(Body["startedAt"].Int(0) == StartUnixMs / 1000 - 30);
	H.transport->Answer("v1/presence", 200, R"({"ok":true,"expiresIn":120})");
	H.Tick();
	CHECK(FirstCalls == 1);
	CHECK(First.Ok());

	H.Advance(59000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 1);
	H.Advance(1000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 2);  // heartbeat before REAN's 120 s expiry
	H.transport->Answer("v1/presence", 200, R"({"ok":true})");
	H.Tick();
	CHECK(FirstCalls == 1);

	// Rapid updates are spaced 5 s apart (REAN allows 120 per 10 minutes).
	A.state = "Round 4";
	REQUIRE(H->SetActivity(A).Ok());
	H.Tick();
	CHECK(H.transport->CountSent("v1/presence", "POST") == 2);
	H.Advance(5000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 3);
	CHECK(H.transport->Last("v1/presence")->JsonBody()["state"].Str() == "Round 4");

	// Clear while that update is in flight: DELETE goes after it.
	H->ClearActivity();
	CHECK_FALSE(H->HasActivity());
	H.Tick();
	CHECK(H.transport->CountSent("v1/presence", "DELETE") == 0);
	H.transport->Answer("v1/presence", 200, R"({"ok":true})");
	H.Tick();
	CHECK(H.transport->CountSent("v1/presence", "DELETE") == 1);
	H.transport->Answer("v1/presence", 200, R"({"ok":true})");
	H.Advance(120000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 3);
}

TEST_CASE("presence: missing presence.write stops the heartbeat; 429 slows it down")
{
	Harness H;
	H.SignIn();
	rean::Activity A;
	A.details = "Menu";
	rean::Error First;
	REQUIRE(H->SetActivity(A, [&](const rean::Error& E) { First = E; }).Ok());
	H.Tick();
	H.transport->Answer("v1/presence", 429, R"({"error":"Too many attempts. Please wait a moment and try again."})", { { "Retry-After", "90" } });
	H.Tick();
	CHECK(First.code == ErrorCode::RateLimited);
	CHECK(H->HasActivity());
	H.Advance(60000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 1);
	H.Advance(30000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 2);
	H.transport->Answer("v1/presence", 403, R"({"error":"insufficient_scope","error_description":"This needs the presence.write scope."})",
		{ { "WWW-Authenticate", "Bearer realm=\"REAN\", error=\"insufficient_scope\"" } });
	H.Tick();
	CHECK_FALSE(H->HasActivity());
	H.Advance(120000);
	CHECK(H.transport->CountSent("v1/presence", "POST") == 2);
}

TEST_CASE("friends: presence, party only in your game, no presence scope")
{
	Harness H;
	H.SignIn();
	std::vector<rean::Friend> F;
	rean::Error Err;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>& List) { Err = E; F = List; });
	H.transport->Answer("v1/friends", 200, R"({"friends":[
	  {"id":"22222222-2222-4222-8222-222222222222","username":"kai","displayName":"Kai","avatarUrl":"https://cdn/kai.png","friendsSince":"2026-08-02T18:11:04Z",
	   "presence":{"status":"in_game","activity":{"app":{"clientId":"rean_test00000000000000000001","name":"Your Game","iconUrl":null},
	     "details":"Ranked","state":"Round 3","startedAt":"2026-11-22T20:15:00.000Z","party":{"id":"lobby-7","size":2,"max":5},"joinable":true}}},
	  {"id":"33333333-3333-4333-8333-333333333333","username":"mo","displayName":"Mo","avatarUrl":null,"friendsSince":null,
	   "presence":{"status":"online","activity":{"app":{"clientId":"rean_other0000000000000000001","name":"Other Game","iconUrl":"https://cdn/o.png"},
	     "details":"Menu","state":null,"startedAt":null}}},
	  {"id":"44444444-4444-4444-8444-444444444444","username":"zed","displayName":"Zed","avatarUrl":null,"friendsSince":null,
	   "presence":{"status":"in_game","activity":{"app":{"clientId":"rean_test00000000000000000001","name":"Your Game","iconUrl":null},
	     "details":null,"state":null,"startedAt":null,"party":null,"joinable":false}}},
	  {"id":"55555555-5555-4555-8555-555555555555","username":"off","displayName":"Off","avatarUrl":null,"friendsSince":null,
	   "presence":{"status":"offline","activity":null}}
	]})");
	H.Tick();
	REQUIRE(Err.Ok());
	REQUIRE(F.size() == 4);
	CHECK(F[0].user.displayName == "Kai");
	CHECK(F[0].friendsSince == "2026-08-02T18:11:04Z");
	CHECK(F[0].hasPresence);
	CHECK(F[0].presence.status == rean::PresenceStatus::InGame);
	CHECK(F[0].presence.hasActivity);
	CHECK(F[0].presence.activity.app.name == "Your Game");
	CHECK(F[0].presence.activity.inYourGame);
	CHECK(F[0].presence.activity.hasParty);
	CHECK(F[0].presence.activity.partyId == "lobby-7");
	CHECK(F[0].presence.activity.partySize == 2);
	CHECK(F[0].presence.activity.partyMax == 5);
	CHECK(F[0].presence.activity.joinable);
	CHECK(F[0].presence.activity.startedAtUnix == 1795378500);
	CHECK_FALSE(F[1].presence.activity.inYourGame);  // another app: no party/join data
	CHECK_FALSE(F[1].presence.activity.hasParty);
	CHECK(F[1].presence.activity.app.iconUrl == "https://cdn/o.png");
	CHECK(F[1].presence.activity.state.empty());
	CHECK(F[2].presence.activity.inYourGame);
	CHECK_FALSE(F[2].presence.activity.hasParty);
	CHECK_FALSE(F[3].presence.hasActivity);
	CHECK(F[3].presence.status == rean::PresenceStatus::Offline);

	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>& List) { Err = E; F = List; });
	H.transport->Answer("v1/friends", 200, R"({"friends":[{"id":"22222222-2222-4222-8222-222222222222","username":"kai","displayName":"Kai","avatarUrl":null,"friendsSince":null}]})");
	H.Tick();
	REQUIRE(F.size() == 1);
	CHECK_FALSE(F[0].hasPresence);  // the app did not ask for presence.read
}

TEST_CASE("presence lookup: validation and parsing")
{
	Harness H;
	H.SignIn();
	rean::Error Err;
	std::vector<rean::PlayerPresence> Got;
	auto Cb = [&](const rean::Error& E, const std::vector<rean::PlayerPresence>& P) { Err = E; Got = P; };
	H->GetPresence({ "not-a-uuid" }, Cb);
	H.Tick();
	CHECK(Err.code == ErrorCode::InvalidArgument);
	std::vector<std::string> Many(101, Bob);
	H->GetPresence(Many, Cb);
	H.Tick();
	CHECK(Err.code == ErrorCode::InvalidArgument);
	H->GetPresence({}, Cb);
	H.Tick();
	CHECK(Err.Ok());
	CHECK(H.transport->CountSent("v1/presence") == 0);
	H->GetPresence({ Bob, "33333333-3333-4333-8333-333333333333" }, Cb);
	const Sent* S = H.transport->Last("v1/presence");
	REQUIRE(S);
	CHECK(S->Path() == "v1/presence?ids=22222222-2222-4222-8222-222222222222%2C33333333-3333-4333-8333-333333333333");
	H.transport->Answer("v1/presence", 200, R"({"presence":[{"playerId":"22222222-2222-4222-8222-222222222222","status":"away","activity":null},{"playerId":"33333333-3333-4333-8333-333333333333","status":"offline","activity":null}]})");
	H.Tick();
	REQUIRE(Got.size() == 2);
	CHECK(Got[0].playerId == Bob);
	CHECK(Got[0].presence.status == rean::PresenceStatus::Away);
	CHECK(Got[1].presence.status == rean::PresenceStatus::Offline);
}

TEST_CASE("invites: send, errors from the Social API, respond")
{
	Harness H;
	H.SignIn();
	rean::Error Err;
	rean::SentInvite SentResult;
	auto Cb = [&](const rean::Error& E, const rean::SentInvite& S) { Err = E; SentResult = S; };
	H->SendInvite("bob", "lobby", "hi", Cb);
	H.Tick();
	CHECK(Err.code == ErrorCode::InvalidArgument);
	H->SendInvite(Alice, "lobby", "hi", Cb);
	H.Tick();
	CHECK(Err.code == ErrorCode::InvalidArgument);  // yourself
	std::string Emoji;
	for (int I = 0; I < 71; I++) Emoji += "\xF0\x9F\x8E\xAE";  // 142 UTF-16 units
	H->SendInvite(Bob, "lobby", Emoji, Cb);
	H.Tick();
	CHECK(Err.code == ErrorCode::InvalidArgument);
	CHECK(H.transport->CountSent("v1/invites") == 0);

	H->SendInvite(Bob, "lobby:7f3a:ticket:93b1", "Ranked? \xF0\x9F\x8E\xAE", Cb);
	const auto* Post = H.transport->Last("v1/invites");
	REQUIRE(Post);
	CHECK(Post->JsonBody()["to"].Str() == Bob);
	CHECK(Post->JsonBody()["joinSecret"].Str() == "lobby:7f3a:ticket:93b1");
	CHECK(Post->JsonBody()["message"].Str() == "Ranked? \xF0\x9F\x8E\xAE");
	H.transport->Answer("v1/invites", 200, R"({"invite":{"id":"c51e0000-0000-4000-8000-000000000001","to":"22222222-2222-4222-8222-222222222222","expiresAt":"2026-11-24T00:10:00Z"}})");
	H.Tick();
	CHECK(Err.Ok());
	CHECK(SentResult.id == "c51e0000-0000-4000-8000-000000000001");

	H->SendInvite(Bob, "", "", Cb);
	CHECK_FALSE(H.transport->Last("v1/invites")->JsonBody().Has("joinSecret"));
	H.transport->Answer("v1/invites", 403, R"({"error":"You can only invite friends.","code":"not_friends"})");
	H.Tick();
	CHECK(Err.code == ErrorCode::NotFriends);
	CHECK(Err.apiCode == "not_friends");
	CHECK(Err.message == "You can only invite friends.");
	CHECK(Err.httpStatus == 403);

	std::string Secret = "unset";
	rean::Error RespondErr;
	H->RespondToInvite("c51e0000-0000-4000-8000-000000000001", true, [&](const rean::Error& E, const std::string& S) { RespondErr = E; Secret = S; });
	CHECK(H.transport->Last("v1/invites/respond")->JsonBody()["accept"].Bool(false));
	H.transport->Answer("v1/invites/respond", 200, R"({"ok":true,"joinSecret":"lobby:7f3a"})");
	H.Tick();
	CHECK(RespondErr.Ok());
	CHECK(Secret == "lobby:7f3a");
	H->RespondToInvite("c51e0000-0000-4000-8000-000000000002", false, [&](const rean::Error& E, const std::string& S) { RespondErr = E; Secret = S; });
	H.transport->Answer("v1/invites/respond", 410, R"({"error":"This invite has expired.","code":"expired"})");
	H.Tick();
	CHECK(RespondErr.code == ErrorCode::Expired);
	H->RespondToInvite("c51e0000-0000-4000-8000-000000000003", true, [&](const rean::Error& E, const std::string&) { RespondErr = E; });
	H.transport->Answer("v1/invites/respond", 404, R"({"error":"Invite not found."})");
	H.Tick();
	CHECK(RespondErr.code == ErrorCode::NotFound);
}

TEST_CASE("errors: scope, rate limit, server, garbage")
{
	Harness H;
	H.SignIn();
	rean::Error Err;
	auto Me = [&](const rean::Error& E, const rean::User&) { Err = E; };
	H->GetMe(Me);
	H.transport->Answer("v1/me", 403, R"({"error":"insufficient_scope","error_description":"This needs the profile scope."})");
	H.Tick();
	CHECK(Err.code == ErrorCode::MissingScope);
	CHECK(Err.message == "This needs the profile scope.");
	H->GetMe(Me);
	H.transport->Answer("v1/me", 429, R"({"error":"Too many attempts. Please wait a moment and try again."})");
	H.Tick();
	CHECK(Err.code == ErrorCode::RateLimited);
	H->GetMe(Me);
	H.transport->Answer("v1/me", 502, "<html>Bad gateway</html>");
	H.Tick();
	CHECK(Err.code == ErrorCode::Server);
	H->GetMe(Me);
	H.transport->Answer("v1/me", 200, "not json");
	H.Tick();
	CHECK(Err.code == ErrorCode::Server);
	H->GetMe(Me);
	H.transport->Answer("v1/me", 200, R"({"id":"11111111-1111-4111-8111-111111111111","username":"alice","displayName":"Alice","avatarUrl":null,"email":"a@x.test"})");
	H.Tick();
	CHECK(Err.Ok());
	CHECK(H->IsSignedIn());
}

TEST_CASE("events: polling, cursor kept per player, stale entries skipped")
{
	Harness H;
	H.SignIn();
	H.Tick();
	CHECK(H.transport->CountSent("v1/events") == 0);  // nobody listens yet
	std::vector<rean::Event> Got;
	H->SetEventCallback([&](const rean::Event& E) { Got.push_back(E); });
	H.Tick();
	REQUIRE(H.transport->Pending("v1/events").size() == 1);
	CHECK(H.transport->Pending("v1/events")[0]->Path() == "v1/events?after=0");
	// Server clock: 2026-11-24T00:00:00Z (StartUnixMs).
	H.transport->Answer("v1/events", 200, R"({"events":[
	  {"id":3,"type":"invite.received","createdAt":"2026-11-20T10:00:00Z","data":{"id":"inv-old","from":{"id":"22222222-2222-4222-8222-222222222222","username":"bob","displayName":"Bob","avatarUrl":null},"message":null,"joinSecret":"s","createdAt":"2026-11-20T10:00:00Z","expiresAt":"2026-11-20T10:10:00Z"}},
	  {"id":4,"type":"invite.accepted","createdAt":"2026-11-23T20:00:00Z","data":{"id":"inv-a","by":{"id":"22222222-2222-4222-8222-222222222222","username":"bob","displayName":"Bob","avatarUrl":null}}},
	  {"id":5,"type":"invite.received","createdAt":"2026-11-23T23:58:00Z","data":{"id":"inv-new","from":{"id":"22222222-2222-4222-8222-222222222222","username":"bob","displayName":"Bob","avatarUrl":null},"message":"Ranked?","joinSecret":"lobby:1","createdAt":"2026-11-23T23:58:00Z","expiresAt":"2026-11-24T00:08:00Z"}},
	  {"id":6,"type":"invite.declined","createdAt":"2026-11-23T23:59:30Z","data":{"id":"inv-b","by":{"id":"22222222-2222-4222-8222-222222222222","username":"bob","displayName":"Bob","avatarUrl":null}}},
	  {"id":7,"type":"party.updated","createdAt":"2026-11-23T23:59:40Z","data":{"x":[1,2]}}
	],"cursor":7})", { { "Date", "Tue, 24 Nov 2026 00:00:00 GMT" } });
	H.Tick();
	REQUIRE(Got.size() == 3);
	CHECK(Got[0].type == rean::EventType::InviteReceived);
	CHECK(Got[0].invite.id == "inv-new");
	CHECK(Got[0].invite.from.displayName == "Bob");
	CHECK(Got[0].invite.message == "Ranked?");
	CHECK(Got[0].invite.joinSecret == "lobby:1");
	CHECK(Got[1].type == rean::EventType::InviteDeclined);
	CHECK(Got[1].inviteId == "inv-b");
	CHECK(Got[1].by.username == "bob");
	CHECK(Got[2].type == rean::EventType::Unknown);
	CHECK(Got[2].typeName == "party.updated");
	CHECK(Got[2].rawData == R"({"x":[1,2]})");
	std::string SavedCursor;
	CHECK(H.store->Load(std::string("rean.events.") + ClientId + "." + Alice, SavedCursor));
	CHECK(SavedCursor == "7");

	H.Advance(9999);
	CHECK(H.transport->CountSent("v1/events") == 1);
	H.Advance(1);
	CHECK(H.transport->Pending("v1/events")[0]->Path() == "v1/events?after=7");
	H.transport->Answer("v1/events", 429, R"({"error":"Too many attempts."})");
	H.Tick();
	H.Advance(10000);
	CHECK(H.transport->CountSent("v1/events") == 2);  // backing off (30 s)
	H.Advance(20000);
	CHECK(H.transport->CountSent("v1/events") == 3);
	H.transport->Answer("v1/events", 200, R"({"events":[],"cursor":7})");
	H.Tick();

	// Next launch: the cursor continues where it stopped.
	H->SignOut();
	H.Tick();
	H.SignIn();
	H.Tick();
	CHECK(H.transport->Last("v1/events")->Path() == "v1/events?after=7");
	H->SetEventCallback(nullptr);
	H.transport->Answer("v1/events", 200, R"({"events":[],"cursor":7})");
	H.Advance(60000);
	CHECK(H.transport->CountSent("v1/events") == 4);
}

TEST_CASE("events: a full page is followed by the next one at once")
{
	Harness H;
	H.SignIn();
	H->SetEventCallback([](const rean::Event&) {});
	H.Tick();
	std::string Page = R"({"events":[)";
	for (int I = 1; I <= 100; I++)
	{
		if (I > 1) Page += ",";
		Page += R"({"id":)" + std::to_string(I) + R"(,"type":"invite.declined","createdAt":"2026-11-01T00:00:00Z","data":{"id":"x","by":{"id":"y"}}})";
	}
	Page += R"(],"cursor":100})";
	H.transport->Answer("v1/events", 200, Page);
	H.Tick(2);
	REQUIRE(H.transport->Pending("v1/events").size() == 1);
	CHECK(H.transport->Pending("v1/events")[0]->Path() == "v1/events?after=100");
}

TEST_CASE("browser: PKCE S256, state check, nonce check")
{
	Harness H;
	SignInResult R;
	std::string Url;
	REQUIRE(H->BeginBrowserSignIn("http://127.0.0.1:53117/callback", Url, R.Callback()).Ok());
	CHECK(Url.compare(0, 41, "https://reanstudios.com/oauth/authorize?c") == 0);
	const auto Q = rean::ParseQuery(Url);
	std::string State, Challenge, Method, Nonce, Redirect, Type, Scope, Cid;
	REQUIRE(rean::FindParam(Q, "state", State));
	REQUIRE(rean::FindParam(Q, "code_challenge", Challenge));
	REQUIRE(rean::FindParam(Q, "code_challenge_method", Method));
	REQUIRE(rean::FindParam(Q, "nonce", Nonce));
	REQUIRE(rean::FindParam(Q, "redirect_uri", Redirect));
	REQUIRE(rean::FindParam(Q, "response_type", Type));
	REQUIRE(rean::FindParam(Q, "scope", Scope));
	REQUIRE(rean::FindParam(Q, "client_id", Cid));
	CHECK(Method == "S256");
	CHECK(Type == "code");
	CHECK(Redirect == "http://127.0.0.1:53117/callback");
	CHECK(Scope == rean::DefaultScopes);
	CHECK(Cid == ClientId);
	CHECK(State.size() >= 22);
	CHECK(Nonce.size() >= 22);
	CHECK(Challenge.size() == 43);

	std::string Dummy;
	SignInResult Other;
	CHECK(H->BeginBrowserSignIn("http://127.0.0.1:1/callback", Dummy, Other.Callback()).code == ErrorCode::Busy);

	CHECK(H->CompleteBrowserSignIn("/callback?code=rac_x&state=forged").code == ErrorCode::InvalidArgument);
	CHECK(H.transport->sent.empty());
	CHECK(H->IsSigningIn());

	REQUIRE(H->CompleteBrowserSignIn("/callback?code=rac_good&state=" + rean::UrlEncode(State) + "&iss=https%3A%2F%2Fapi.reanstudios.com").Ok());
	CHECK(H->CompleteBrowserSignIn("/callback?code=rac_good&state=" + rean::UrlEncode(State)).code == ErrorCode::Busy);  // codes are exchanged once
	const Sent* T = H.transport->Last("oauth/token");
	REQUIRE(T);
	CHECK(T->Form("grant_type") == "authorization_code");
	CHECK(T->Form("code") == "rac_good");
	CHECK(T->Form("redirect_uri") == "http://127.0.0.1:53117/callback");
	CHECK(rean::PkceChallenge(T->Form("code_verifier")) == Challenge);
	CHECK(T->Form("code_verifier").size() == 43);

	H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_web", Alice, ",\"nonce\":\"" + Nonce + "\""));
	H.Tick();
	CHECK(R.error.Ok());
	CHECK(H->IsSignedIn());
}

TEST_CASE("browser: nonce mismatch, declined, timeout")
{
	{
		Harness H;
		SignInResult R;
		std::string Url, State;
		REQUIRE(H->BeginBrowserSignIn("http://127.0.0.1:5/callback", Url, R.Callback()).Ok());
		rean::FindParam(rean::ParseQuery(Url), "state", State);
		REQUIRE(H->CompleteBrowserSignIn("code=rac_1&state=" + rean::UrlEncode(State)).Ok());
		H.transport->Answer("oauth/token", 200, TokenAnswer("rrt_web", Alice, ",\"nonce\":\"someone-else\""));
		H.Tick();
		CHECK(R.error.code == ErrorCode::InvalidGrant);
		CHECK_FALSE(H->IsSignedIn());
		CHECK(H.Saved().empty());
	}
	{
		Harness H;
		SignInResult R;
		std::string Url, State;
		REQUIRE(H->BeginBrowserSignIn("http://127.0.0.1:5/callback", Url, R.Callback()).Ok());
		rean::FindParam(rean::ParseQuery(Url), "state", State);
		REQUIRE(H->CompleteBrowserSignIn("http://127.0.0.1:5/callback?error=access_denied&error_description=The+player+declined.&state=" +
			rean::UrlEncode(State)).Ok());
		H.Tick();
		CHECK(R.error.code == ErrorCode::Denied);
		CHECK(R.error.message == "The player declined.");
	}
	{
		Harness H;
		SignInResult R;
		std::string Url;
		REQUIRE(H->BeginBrowserSignIn("http://127.0.0.1:5/callback", Url, R.Callback()).Ok());
		H.Advance(10 * 60 * 1000);
		CHECK(R.error.code == ErrorCode::Expired);
		CHECK_FALSE(H->IsSigningIn());
	}
	{
		rean::Config C;
		C.clientId = ClientId;
		C.transport = std::make_shared<FakeTransport>();
		C.randomBytes = [](uint8_t*, size_t) { return false; };
		rean::Client NoRandom(C);
		std::string Url;
		CHECK(NoRandom.BeginBrowserSignIn("http://127.0.0.1:5/callback", Url, nullptr).code == ErrorCode::NotSupported);
	}
}

TEST_CASE("id token for another app is refused")
{
	Harness H;
	SignInResult R;
	H->SignInWithLauncher("rlc_aud_check", R.Callback());
	const std::string WrongAud = std::string("{\"access_token\":\"a.b.c\",\"expires_in\":3600,\"refresh_token\":\"rrt_z\",\"id_token\":\"") +
		Jwt(R"({"aud":"rean_someoneelse00000000000001","sub":"11111111-1111-4111-8111-111111111111"})") + "\"}";
	H.transport->Answer("oauth/token", 200, WrongAud);
	H.Tick();
	CHECK(R.error.code == ErrorCode::InvalidGrant);
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.Saved().empty());
}

TEST_CASE("no offline_access: no saved session, signed out when the access token runs out")
{
	Harness H(ClientId, "openid profile friends.read");
	bool Warned = false;
	for (const std::string& L : H.logs) Warned = Warned || L.find("offline_access") != std::string::npos;
	CHECK(Warned);
	SignInResult R;
	H->SignInWithLauncher("rlc_no_offline", R.Callback());
	H.transport->Answer("oauth/token", 200, TokenAnswer(""));
	H.Tick();
	REQUIRE(R.error.Ok());
	CHECK(H.Saved().empty());
	H.monotonic += 3600 * 1000;
	rean::Error Err;
	H->GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>&) { Err = E; });
	H.Tick();
	CHECK(Err.code == ErrorCode::SessionEnded);
	CHECK_FALSE(H->IsSignedIn());
	CHECK(H.transport->CountSent("oauth/token") == 1);
}

TEST_CASE("transport may answer inside Send; callbacks still run in Tick")
{
	Harness H;
	H.transport->answerInsideSend = true;
	H.transport->syncStatus = 400;
	H.transport->syncBody = OAuthError("invalid_grant", "The code has expired.");
	SignInResult R;
	H->SignInWithLauncher("rlc_sync_answer", R.Callback());
	CHECK(R.calls == 0);
	H.Tick();
	CHECK(R.calls == 1);
	CHECK(R.error.code == ErrorCode::InvalidGrant);
}

TEST_CASE("destroying the client with requests in flight is safe")
{
	auto Transport = std::make_shared<FakeTransport>();
	{
		rean::Config C;
		C.clientId = ClientId;
		C.transport = Transport;
		rean::Client Client(C);
		Client.SignInWithDevice([](const rean::DeviceCode&) {}, [](const rean::Error&, const rean::User&) {});
		Client.Tick();
	}
	REQUIRE(Transport->sent.size() == 1);
	Transport->Answer("oauth/device", 200, DeviceAnswer);  // completes after the client is gone
	CHECK(Transport->sent[0].answered);
}

TEST_CASE("a callback may sign out or replace the event handler while events are delivered")
{
	Harness H;
	H.SignIn();
	int Seen = 0;
	H->SetEventCallback([&](const rean::Event&) {
		Seen++;
		H->SignOut();
	});
	H.Tick();
	H.transport->Answer("v1/events", 200, R"({"events":[{"id":1,"type":"invite.declined","createdAt":"2026-11-23T23:59:59Z","data":{"id":"a","by":{"id":"b"}}},{"id":2,"type":"invite.declined","createdAt":"2026-11-23T23:59:59Z","data":{"id":"c","by":{"id":"d"}}}],"cursor":2})");
	H.Tick();
	CHECK(Seen == 1);  // signed out inside the first: the second is not delivered
	CHECK_FALSE(H->IsSignedIn());
}
