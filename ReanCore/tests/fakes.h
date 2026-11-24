// Copyright (c) REAN Studios. MIT License.
//
// Test doubles: a transport that records requests and lets the test answer
// them, a controllable clock and helpers to build REAN-shaped answers.
#pragma once

#include "Rean/ReanClient.h"
#include "ReanCrypto.h"
#include "ReanJson.h"
#include "ReanText.h"

#include <memory>
#include <string>
#include <vector>

namespace reantest
{

constexpr const char* ClientId = "rean_test00000000000000000001";
constexpr const char* Alice = "11111111-1111-4111-8111-111111111111";
constexpr const char* Bob = "22222222-2222-4222-8222-222222222222";
constexpr int64_t StartUnixMs = 1795478400000LL;  // 2026-11-24T00:00:00Z

struct Sent
{
	rean::HttpRequest request;
	rean::HttpDone done;
	bool answered = false;

	std::string Path() const
	{
		const size_t At = request.url.find("/api/");
		return At == std::string::npos ? request.url : request.url.substr(At + 5);
	}
	std::string HeaderValue(const char* Name) const
	{
		for (const auto& H : request.headers)
		{
			if (rean::ToLowerAscii(H.name) == rean::ToLowerAscii(Name)) return H.value;
		}
		return std::string();
	}
	std::string Form(const char* Key) const
	{
		std::string V;
		rean::FindParam(rean::ParseQuery("?" + request.body), Key, V);
		return V;
	}
	rean::Json JsonBody() const
	{
		rean::Json J;
		rean::Json::Parse(request.body, J);
		return J;
	}
};

class FakeTransport : public rean::IHttpTransport
{
public:
	std::vector<Sent> sent;
	bool answerInsideSend = false;  // completes synchronously from inside Send (allowed by the contract)
	int syncStatus = 200;
	std::string syncBody = "{}";

	void Send(const rean::HttpRequest& Request, rean::HttpDone Done) override
	{
		sent.push_back(Sent{ Request, Done, false });
		if (answerInsideSend)
		{
			sent.back().answered = true;
			rean::HttpResponse R;
			R.status = syncStatus;
			R.body = syncBody;
			Done(R);
		}
	}

	/** Pending (unanswered) requests whose path starts with Prefix. */
	std::vector<Sent*> Pending(const std::string& Prefix = std::string())
	{
		std::vector<Sent*> Out;
		for (Sent& S : sent)
		{
			if (!S.answered && S.Path().compare(0, Prefix.size(), Prefix) == 0) Out.push_back(&S);
		}
		return Out;
	}

	size_t CountSent(const std::string& Prefix, const char* Method = nullptr) const
	{
		size_t N = 0;
		for (const Sent& S : sent)
		{
			if (S.Path().compare(0, Prefix.size(), Prefix) == 0 && (!Method || S.request.method == Method)) N++;
		}
		return N;
	}

	/** Answers the oldest pending request with this path prefix. Returns false if there is none. */
	bool Answer(const std::string& Prefix, int Status, const std::string& Body, std::vector<rean::HttpHeader> Headers = {})
	{
		for (Sent& S : sent)
		{
			if (S.answered || S.Path().compare(0, Prefix.size(), Prefix) != 0) continue;
			S.answered = true;
			rean::HttpResponse R;
			R.status = Status;
			R.body = Body;
			R.headers = std::move(Headers);
			S.done(R);
			return true;
		}
		return false;
	}

	Sent* Last(const std::string& Prefix)
	{
		for (auto It = sent.rbegin(); It != sent.rend(); ++It)
		{
			if (It->Path().compare(0, Prefix.size(), Prefix) == 0) return &*It;
		}
		return nullptr;
	}
};

inline std::string B64(const std::string& S) { return rean::Base64UrlEncode(S); }

inline std::string Jwt(const std::string& PayloadJson)
{
	return B64("{\"alg\":\"RS256\",\"typ\":\"JWT\",\"kid\":\"k1\"}") + "." + B64(PayloadJson) + ".c2lnbmF0dXJl";
}

inline std::string IdToken(const std::string& Sub, const std::string& Username, const std::string& Name, const std::string& Extra = std::string())
{
	return Jwt(std::string("{\"iss\":\"https://api.reanstudios.com\",\"aud\":\"") + ClientId + "\",\"sub\":\"" + Sub +
	           "\",\"preferred_username\":\"" + Username + "\",\"name\":\"" + Name + "\"" + Extra + "}");
}

inline std::string TokenAnswer(const std::string& Refresh, const std::string& Sub = Alice, const std::string& Extra = std::string())
{
	std::string Out = "{\"access_token\":\"" + Jwt(std::string("{\"sub\":\"") + Sub + "\",\"scope\":\"x\"}") +
	                  "\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"openid profile offline_access friends.read presence.read presence.write invites\"";
	Out += ",\"id_token\":\"" + IdToken(Sub, Sub == Alice ? "alice" : "bob", Sub == Alice ? "Alice" : "Bob", Extra) + "\"";
	if (!Refresh.empty()) Out += ",\"refresh_token\":\"" + Refresh + "\"";
	return Out + "}";
}

inline std::string OAuthError(const char* Error, const char* Description = "Something.")
{
	return std::string("{\"error\":\"") + Error + "\",\"error_description\":\"" + Description + "\"}";
}

struct Harness
{
	std::shared_ptr<FakeTransport> transport = std::make_shared<FakeTransport>();
	std::shared_ptr<rean::MemoryStore> store = std::make_shared<rean::MemoryStore>();
	int64_t monotonic = 5000000;
	int64_t unix = StartUnixMs;
	std::vector<std::string> logs;
	std::unique_ptr<rean::Client> client;

	explicit Harness(std::string ClientIdValue = ClientId, std::string Scopes = rean::DefaultScopes)
	{
		rean::Config C;
		C.clientId = std::move(ClientIdValue);
		C.scopes = std::move(Scopes);
		C.transport = transport;
		C.store = store;
		C.monotonicMs = [this]() { return monotonic; };
		C.unixMs = [this]() { return unix; };
		C.log = [this](rean::LogLevel, const std::string& M) { logs.push_back(M); };
		client.reset(new rean::Client(std::move(C)));
	}

	rean::Client& operator*() { return *client; }
	rean::Client* operator->() { return client.get(); }
	void Tick(int Times = 1)
	{
		for (int I = 0; I < Times; I++) client->Tick();
	}
	void Advance(int64_t Ms)
	{
		monotonic += Ms;
		unix += Ms;
		Tick();
	}
	std::string SessionKey() const { return std::string("rean.session.") + ClientId; }
	std::string Saved()
	{
		std::string V;
		store->Load(SessionKey(), V);
		return V;
	}

	/** Signs Alice in with a launcher code (unique per call). */
	void SignIn(const std::string& Refresh = "rrt_first")
	{
		static int Counter = 0;
		rean::Error Result = rean::Error::Make(rean::ErrorCode::Server, "not called");
		client->SignInWithLauncher("rlc_harness_" + std::to_string(++Counter), [&Result](const rean::Error& E, const rean::User&) { Result = E; });
		Tick();
		transport->Answer("oauth/token", 200, TokenAnswer(Refresh));
		Tick();
	}
};

} // namespace reantest
