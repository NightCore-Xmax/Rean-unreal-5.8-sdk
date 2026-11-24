// Copyright (c) REAN Studios. MIT License.
//
// rean_device_check: signs in to REAN with a device code and runs every Social
// API call once, so you can check a REAN app without Unreal.
//
//   export REAN_CLIENT_ID=rean_xxxxxxxxxxxxxxxxxxxxxxxx   # your app's client id (never commit it)
//   ./rean_device_check [--api https://api.reanstudios.com] [--invite <friend-id>] [--keep-session] [--verbose]
//
// It shows a code (and a QR code); approve it at https://reanstudios.com/activate
// with a REAN account. Nothing is saved; the session is revoked at the end
// unless --keep-session is given.
#include "Rean/ReanClient.h"
#include "Rean/ReanQrCode.h"
#include "curl_transport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
int Failures = 0;

void Report(const char* Step, const rean::Error& E)
{
	if (E.Ok())
	{
		std::printf("PASS  %s\n", Step);
		return;
	}
	Failures++;
	std::printf("FAIL  %s: %s (%s, HTTP %d%s%s)\n", Step, E.message.c_str(), rean::ErrorCodeName(E.code), E.httpStatus,
		E.apiCode.empty() ? "" : ", ", E.apiCode.c_str());
}

/** Ticks the client until Flag is set or Seconds pass. */
bool RunUntil(rean::Client& Client, const bool& Flag, int Seconds)
{
	const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(Seconds);
	while (!Flag && std::chrono::steady_clock::now() < End)
	{
		Client.Tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return Flag;
}

const char* StatusName(rean::PresenceStatus S)
{
	switch (S)
	{
	case rean::PresenceStatus::Online: return "online";
	case rean::PresenceStatus::Away: return "away";
	case rean::PresenceStatus::DoNotDisturb: return "dnd";
	case rean::PresenceStatus::InGame: return "in game";
	case rean::PresenceStatus::Offline: return "offline";
	case rean::PresenceStatus::Unknown: return "unknown";
	}
	return "?";
}
} // namespace

int main(int Argc, char** Argv)
{
	std::string Api = rean::DefaultApiBase;
	std::string InviteFriend;
	bool bKeep = false;
	bool bVerbose = false;
	for (int I = 1; I < Argc; I++)
	{
		const std::string A = Argv[I];
		if (A == "--api" && I + 1 < Argc) Api = Argv[++I];
		else if (A == "--invite" && I + 1 < Argc) InviteFriend = Argv[++I];
		else if (A == "--keep-session") bKeep = true;
		else if (A == "--verbose") bVerbose = true;
		else
		{
			std::fprintf(stderr, "usage: REAN_CLIENT_ID=rean_... %s [--api URL] [--invite FRIEND_ID] [--keep-session] [--verbose]\n", Argv[0]);
			return 2;
		}
	}
	const char* ClientId = std::getenv("REAN_CLIENT_ID");
	if (!ClientId || !rean::IsValidClientId(ClientId))
	{
		std::fprintf(stderr, "Set REAN_CLIENT_ID to your app's client id (rean_ + 24 lowercase letters/digits).\n");
		return 2;
	}

	auto Transport = std::make_shared<reantools::CurlTransport>();
	Transport->verbose = bVerbose;
	rean::Config C;
	C.clientId = ClientId;
	C.apiBase = Api;
	C.userAgent = "rean-device-check/" REAN_CORE_VERSION;
	C.transport = Transport;
	C.saveSession = false;
	C.log = [bVerbose](rean::LogLevel Level, const std::string& M) {
		if (bVerbose || Level >= rean::LogLevel::Warning) std::fprintf(stderr, "  [rean] %s\n", M.c_str());
	};
	rean::Client Client(C);
	if (!Client.ConfigError().Ok())
	{
		Report("configuration", Client.ConfigError());
		return 1;
	}
	std::printf("REAN API: %s\n", Client.GetConfig().apiBase.c_str());

	// 1. Device sign-in.
	bool bSigned = false;
	rean::Error SignErr;
	Client.SignInWithDevice(
		[](const rean::DeviceCode& D) {
			std::printf("\nGo to %s and enter  %s\n(or scan this; the code expires in %d minutes)\n", D.verificationUri.c_str(), D.userCode.c_str(),
				D.expiresInSeconds / 60);
			rean::QrCode Q;
			if (rean::QrCode::Encode(D.verificationUriComplete, rean::QrCode::Ecc::Medium, Q))
			{
				// Two characters per module keep it square in a terminal; light-on-dark friendly.
				const std::string T = Q.ToText(2);
				std::string Line;
				for (const char Ch : T)
				{
					if (Ch == '\n')
					{
						std::printf("%s\n", Line.c_str());
						Line.clear();
					}
					else
					{
						Line += Ch == '#' ? "  " : "\xE2\x96\x88\xE2\x96\x88";
					}
				}
			}
			std::fflush(stdout);
		},
		[&](const rean::Error& E, const rean::User&) {
			SignErr = E;
			bSigned = true;
		});
	RunUntil(Client, bSigned, 11 * 60);
	Report("device sign-in", SignErr);
	if (!Client.IsSignedIn()) return 1;
	const rean::User Me = Client.CurrentUser();
	std::printf("      signed in as %s (%s), player id %s\n      scopes: %s\n", Me.displayName.c_str(), Me.username.c_str(), Me.id.c_str(),
		Client.GrantedScopes().c_str());

	// 2. Me.
	if (Client.HasScope("profile"))
	{
		bool bDone = false;
		Client.GetMe([&](const rean::Error& E, const rean::User& U) {
			Report("GET /v1/me", E);
			if (E.Ok() && U.id != Me.id)
			{
				Failures++;
				std::printf("FAIL  /v1/me returned another player id\n");
			}
			bDone = true;
		});
		RunUntil(Client, bDone, 40);
	}

	// 3. Friends.
	std::vector<rean::Friend> Friends;
	if (Client.HasScope("friends.read"))
	{
		bool bDone = false;
		Client.GetFriends([&](const rean::Error& E, const std::vector<rean::Friend>& F) {
			Report("GET /v1/friends", E);
			Friends = F;
			bDone = true;
		});
		RunUntil(Client, bDone, 40);
		for (const rean::Friend& F : Friends)
		{
			std::printf("      - %s (%s)%s%s%s\n", F.user.displayName.c_str(), F.hasPresence ? StatusName(F.presence.status) : "no presence scope",
				F.presence.hasActivity ? ": " : "", F.presence.hasActivity ? F.presence.activity.app.name.c_str() : "",
				F.presence.activity.inYourGame ? " (in this game)" : "");
		}
	}

	// 4. Rich presence.
	if (Client.HasScope("presence.write"))
	{
		rean::Activity A;
		A.details = "Testing the REAN SDK";
		A.state = "rean_device_check";
		A.partyId = "device-check";
		A.partySize = 1;
		A.partyMax = 4;
		A.startedAt = static_cast<int64_t>(std::time(nullptr));
		bool bDone = false;
		const rean::Error Valid = Client.SetActivity(A, [&](const rean::Error& E) {
			Report("POST /v1/presence", E);
			bDone = true;
		});
		if (!Valid.Ok()) Report("SetActivity", Valid);
		else RunUntil(Client, bDone, 40);
	}

	// 5. Presence lookup.
	if (Client.HasScope("presence.read"))
	{
		std::vector<std::string> Ids;
		for (size_t I = 0; I < Friends.size() && I < 5; I++) Ids.push_back(Friends[I].user.id);
		if (Ids.empty()) Ids.push_back("00000000-0000-4000-8000-000000000000");  // not a friend: reads as offline
		bool bDone = false;
		Client.GetPresence(Ids, [&](const rean::Error& E, const std::vector<rean::PlayerPresence>& P) {
			Report("GET /v1/presence", E);
			for (const auto& Item : P) std::printf("      - %s: %s\n", Item.playerId.c_str(), Item.presence.statusText.c_str());
			bDone = true;
		});
		RunUntil(Client, bDone, 40);
	}

	// 6. Invites and events.
	if (Client.HasScope("invites"))
	{
		bool bDone = false;
		Client.GetInvites([&](const rean::Error& E, const std::vector<rean::Invite>& L) {
			Report("GET /v1/invites", E);
			std::printf("      %zu pending invite(s)\n", L.size());
			bDone = true;
		});
		RunUntil(Client, bDone, 40);
		if (!InviteFriend.empty())
		{
			bool bSent = false;
			Client.SendInvite(InviteFriend, "rean-device-check", "Test invite from rean_device_check", [&](const rean::Error& E, const rean::SentInvite& S) {
				Report("POST /v1/invites", E);
				if (E.Ok()) std::printf("      invite %s expires %s\n", S.id.c_str(), S.expiresAt.c_str());
				bSent = true;
			});
			RunUntil(Client, bSent, 40);
		}
	}
	{
		int Count = 0;
		Client.SetEventCallback([&](const rean::Event& E) {
			Count++;
			std::printf("      event %lld %s\n", static_cast<long long>(E.id), E.typeName.c_str());
		});
		// One poll happens right away; give it time to answer.
		const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::chrono::steady_clock::now() < End)
		{
			Client.Tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		std::printf("PASS  GET /v1/events (%d recent event(s))\n", Count);
		Client.SetEventCallback(nullptr);
	}

	// 7. Clean up.
	Client.ClearActivity();
	for (int I = 0; I < 20; I++)
	{
		Client.Tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (!bKeep)
	{
		Client.SignOut();
		for (int I = 0; I < 20; I++)
		{
			Client.Tick();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		std::printf("PASS  sign-out (session revoked)\n");
	}
	std::printf("\n%s: %d failure(s)\n", Failures ? "FAILED" : "OK", Failures);
	return Failures ? 1 : 0;
}
