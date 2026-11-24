// Copyright (c) REAN Studios. MIT License.
#include "doctest.h"

#include "ReanJson.h"

using rean::Json;

TEST_CASE("json: parses REAN-shaped objects and chains missing keys to null")
{
	Json J;
	REQUIRE(Json::Parse(R"({"friends":[{"id":"8e2d","presence":{"status":"in_game","activity":{"party":{"id":"lobby-7","size":2,"max":null}}}}],"n":-12.5e1,"t":true})", J));
	CHECK(J["friends"].Size() == 1);
	CHECK(J["friends"].At(0)["presence"]["activity"]["party"]["id"].Str() == "lobby-7");
	CHECK(J["friends"].At(0)["presence"]["activity"]["party"]["size"].Int(-1) == 2);
	CHECK(J["friends"].At(0)["presence"]["activity"]["party"]["max"].Int(-1) == -1);
	CHECK(J["n"].Num(0) == doctest::Approx(-125.0));
	CHECK(J["t"].Bool(false));
	CHECK(J["missing"]["deeper"].At(3)["x"].Str("fallback") == "fallback");
	CHECK(J["friends"].At(5).IsNull());
	CHECK_FALSE(J.Has("missing"));
	CHECK(J.Has("t"));
}

TEST_CASE("json: string escapes, unicode and surrogate pairs")
{
	Json J;
	REQUIRE(Json::Parse(R"(["a\"b\\c\/d\n", "\u00e9\u4e2d", "\ud83c\udfae", "\ud800x"])", J));
	CHECK(J.At(0).Str() == "a\"b\\c/d\n");
	CHECK(J.At(1).Str() == "\xC3\xA9\xE4\xB8\xAD");
	CHECK(J.At(2).Str() == "\xF0\x9F\x8E\xAE");       // 🎮
	CHECK(J.At(3).Str() == "\xEF\xBF\xBDx");            // lone surrogate -> U+FFFD
}

TEST_CASE("json: rejects malformed input")
{
	Json J;
	const char* Bad[] = { "", "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "01", "1.", "-", "\"unterminated", "\"tab\there\"", "nul", "{} x",
		                  "{\"a\":\"\\q\"}", "[1 2]" };
	for (const char* B : Bad)
	{
		CAPTURE(B);
		CHECK_FALSE(Json::Parse(B, J));
		CHECK(J.IsNull());
	}
}

TEST_CASE("json: depth limit protects the stack")
{
	std::string Deep(200, '[');
	Deep += std::string(200, ']');
	Json J;
	CHECK_FALSE(Json::Parse(Deep, J));
	std::string Ok(30, '[');
	Ok += std::string(30, ']');
	CHECK(Json::Parse(Ok, J));
}

TEST_CASE("json: numbers are parsed without the C locale")
{
	Json J;
	REQUIRE(Json::Parse("[0, 3600, 1795420800, 0.5, 1e3, 12345678901234567890, -0.001]", J));
	CHECK(J.At(1).Int(0) == 3600);
	CHECK(J.At(2).Int(0) == 1795420800);
	CHECK(J.At(3).Num(0) == doctest::Approx(0.5));
	CHECK(J.At(4).Int(0) == 1000);
	CHECK(J.At(5).Num(0) == doctest::Approx(1.2345678901234567e19));
	CHECK(J.At(5).Int(-7) == -7);  // out of int64 range
	CHECK(J.At(6).Num(0) == doctest::Approx(-0.001));
}

TEST_CASE("json: writer escapes and round-trips")
{
	rean::JsonObjectWriter W;
	W.Str("to", "8e2d").Str("message", "Ranked? \"go\"\n\x01").Int("n", -5).Bool("joinable", true);
	const std::string Out = W.Finish();
	CHECK(Out == R"({"to":"8e2d","message":"Ranked? \"go\"\n\u0001","n":-5,"joinable":true})");
	Json J;
	REQUIRE(Json::Parse(Out, J));
	CHECK(J["message"].Str() == "Ranked? \"go\"\n\x01");
	CHECK(J.Dump() == Out);
}
