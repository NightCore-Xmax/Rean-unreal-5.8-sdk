// Copyright (c) REAN Studios. MIT License.
#include "doctest.h"

#include "Rean/ReanClient.h"
#include "ReanText.h"

TEST_CASE("url: encoding and form bodies")
{
	CHECK(rean::UrlEncode("abc-._~XYZ019") == "abc-._~XYZ019");
	CHECK(rean::UrlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
	CHECK(rean::FormEncode({ { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" }, { "scope", "openid profile" } }) ==
	      "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&scope=openid%20profile");
	CHECK(rean::UrlDecode("a+b%20c%2x%") == "a b c%2x%");
}

TEST_CASE("url: query parsing for browser callbacks")
{
	const auto Q = rean::ParseQuery("http://127.0.0.1:53117/callback?code=rac_abc&state=xy%2Fz&iss=https%3A%2F%2Fapi.reanstudios.com#frag");
	std::string V;
	REQUIRE(rean::FindParam(Q, "code", V));
	CHECK(V == "rac_abc");
	REQUIRE(rean::FindParam(Q, "state", V));
	CHECK(V == "xy/z");
	REQUIRE(rean::FindParam(Q, "iss", V));
	CHECK(V == "https://api.reanstudios.com");
	CHECK_FALSE(rean::FindParam(Q, "frag", V));
	CHECK(rean::ParseQuery("?a=1&b").size() == 2);
	CHECK(rean::ParseQuery("/callback?error=access_denied&error_description=The+player+declined.").size() == 2);
	CHECK(rean::ParseQuery("a=1&&b=2").size() == 2);
	CHECK(rean::ParseQuery("http://127.0.0.1/callback").empty());
}

TEST_CASE("time: ISO 8601 as REAN and Postgres send it")
{
	int64_t Ms = 0;
	REQUIRE(rean::ParseIso8601Ms("1970-01-01T00:00:00Z", Ms));
	CHECK(Ms == 0);
	REQUIRE(rean::ParseIso8601Ms("2026-11-22T20:15:00Z", Ms));
	CHECK(Ms == 1795378500000LL);
	REQUIRE(rean::ParseIso8601Ms("2026-11-22T20:15:00.250Z", Ms));
	CHECK(Ms == 1795378500250LL);
	REQUIRE(rean::ParseIso8601Ms("2026-11-22 21:15:00.123456+01:00", Ms));
	CHECK(Ms == 1795378500123LL);
	REQUIRE(rean::ParseIso8601Ms("2026-11-22T15:15:00-0500", Ms));
	CHECK(Ms == 1795378500000LL);
	REQUIRE(rean::ParseIso8601Ms("2024-02-29T12:00:00", Ms));  // no zone: UTC
	CHECK(Ms == 1709208000000LL);
	CHECK_FALSE(rean::ParseIso8601Ms("", Ms));
	CHECK_FALSE(rean::ParseIso8601Ms("2026-13-01T00:00:00Z", Ms));
	CHECK_FALSE(rean::ParseIso8601Ms("2026-11-22", Ms));
	CHECK_FALSE(rean::ParseIso8601Ms("2026-11-22T20:15:00Zjunk", Ms));
}

TEST_CASE("time: HTTP Date header")
{
	int64_t Ms = 0;
	REQUIRE(rean::ParseHttpDateMs("Sun, 06 Nov 1994 08:49:37 GMT", Ms));
	CHECK(Ms == 784111777000LL);
	CHECK_FALSE(rean::ParseHttpDateMs("Sunday, 06-Nov-94 08:49:37 GMT", Ms));
	CHECK_FALSE(rean::ParseHttpDateMs("", Ms));
}

TEST_CASE("ids: client ids and player ids")
{
	CHECK(rean::IsValidClientId("rean_xxxxxxxxxxxxxxxxxxxxxxxx"));
	CHECK(rean::IsValidClientId("rean_0123456789abcdefghijklmn"));
	CHECK_FALSE(rean::IsValidClientId("rean_XXXXXXXXXXXXXXXXXXXXXXXX"));
	CHECK_FALSE(rean::IsValidClientId("rean_short"));
	CHECK_FALSE(rean::IsValidClientId("reax_xxxxxxxxxxxxxxxxxxxxxxxx"));
	CHECK_FALSE(rean::IsValidClientId(" rean_xxxxxxxxxxxxxxxxxxxxxxxx"));
	CHECK(rean::IsUuid("4f9c2b1e-7a3d-4c55-9e10-2f6b8a1d3c77"));
	CHECK(rean::IsUuid("4F9C2B1E-7A3D-4C55-9E10-2F6B8A1D3C77"));
	CHECK_FALSE(rean::IsUuid("4f9c2b1e7a3d4c559e102f6b8a1d3c77"));
	CHECK_FALSE(rean::IsUuid("4f9c2b1e-7a3d-4c55-9e10-2f6b8a1d3c7g"));
}

TEST_CASE("launcher: -ReanAuthCode in its spellings, not -LauncherToken")
{
	CHECK(rean::FindLauncherCode({ "Game.exe", "-ReanAuthCode=rlc_abc", "-LauncherToken=eyJ" }) == "rlc_abc");
	CHECK(rean::FindLauncherCode({ "Game.exe", "-reanauthcode", "rlc_def" }) == "rlc_def");
	CHECK(rean::FindLauncherCode({ "Game.exe", "--rean-auth-code=\"rlc_q\"" }) == "rlc_q");
	CHECK(rean::FindLauncherCode({ "Game.exe", "-LauncherToken=eyJ" }).empty());
	CHECK(rean::FindLauncherCode({ "Game.exe", "-ReanAuthCodeX=1" }).empty());
	CHECK(rean::FindLauncherCode({}).empty());
}

TEST_CASE("utf16: limits are counted the way REAN counts them")
{
	CHECK(rean::Utf16Length("") == 0);
	CHECK(rean::Utf16Length("Ranked") == 6);
	CHECK(rean::Utf16Length("\xC3\xA9") == 1);             // é
	CHECK(rean::Utf16Length("\xE4\xB8\xAD") == 1);         // 中
	CHECK(rean::Utf16Length("\xF0\x9F\x8E\xAE") == 2);     // 🎮 is a surrogate pair
}

TEST_CASE("loopback: request line and response page")
{
	std::string Method, Target;
	REQUIRE(rean::ParseHttpRequestTarget("GET /callback?code=a&state=b HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", Method, Target));
	CHECK(Method == "GET");
	CHECK(Target == "/callback?code=a&state=b");
	CHECK_FALSE(rean::ParseHttpRequestTarget("garbage", Method, Target));
	CHECK_FALSE(rean::ParseHttpRequestTarget("GET  HTTP/1.1\r\n", Method, Target));
	const std::string Page = rean::LoopbackResponse(true);
	const size_t Split = Page.find("\r\n\r\n");
	REQUIRE(Split != std::string::npos);
	const size_t LenAt = Page.find("Content-Length: ");
	REQUIRE(LenAt != std::string::npos);
	const size_t Declared = static_cast<size_t>(std::stoul(Page.substr(LenAt + 16)));
	CHECK(Declared == Page.size() - Split - 4);
	CHECK(Page.find("go back to the game") != std::string::npos);
}
