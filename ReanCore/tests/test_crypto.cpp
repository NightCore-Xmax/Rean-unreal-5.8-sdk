// Copyright (c) REAN Studios. MIT License.
#include "doctest.h"

#include "ReanCrypto.h"

#include <string>

namespace
{
std::string Hex(const std::string& Bytes)
{
	static const char* const H = "0123456789abcdef";
	std::string Out;
	for (const char C : Bytes)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		Out.push_back(H[U >> 4]);
		Out.push_back(H[U & 15]);
	}
	return Out;
}
} // namespace

TEST_CASE("sha256: FIPS 180-2 test vectors")
{
	CHECK(Hex(rean::Sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Hex(rean::Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(Hex(rean::Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
	      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	CHECK(Hex(rean::Sha256(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	// Lengths around the 55/56/64 byte padding boundaries.
	CHECK(Hex(rean::Sha256(std::string(55, 'a'))) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	CHECK(Hex(rean::Sha256(std::string(56, 'a'))) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
	CHECK(Hex(rean::Sha256(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("pkce: RFC 7636 appendix B example")
{
	CHECK(rean::PkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST_CASE("base64url: round trip, no padding, tolerant decoding")
{
	for (size_t Len = 0; Len < 70; Len++)
	{
		std::string Raw;
		for (size_t I = 0; I < Len; I++) Raw.push_back(static_cast<char>((I * 37 + 11) & 0xFF));
		const std::string Enc = rean::Base64UrlEncode(Raw);
		CHECK(Enc.find('=') == std::string::npos);
		CHECK(Enc.find('+') == std::string::npos);
		CHECK(Enc.find('/') == std::string::npos);
		std::string Back;
		REQUIRE(rean::Base64UrlDecode(Enc, Back));
		CHECK(Back == Raw);
	}
	std::string Out;
	CHECK(rean::Base64UrlDecode("eyJhIjoxfQ==", Out));
	CHECK(Out == "{\"a\":1}");
	CHECK(rean::Base64UrlDecode("+/8", Out));  // standard alphabet accepted too
	CHECK_FALSE(rean::Base64UrlDecode("ab$c", Out));
	CHECK_FALSE(rean::Base64UrlDecode("a", Out));
}
