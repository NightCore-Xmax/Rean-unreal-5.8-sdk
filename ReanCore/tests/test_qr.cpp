// Copyright (c) REAN Studios. MIT License.
//
// Structural checks here; tests/qr_decode_check.py decodes the images this test
// writes (set REAN_QR_DUMP=<dir>) with an independent decoder (OpenCV).
#include "doctest.h"

#include "Rean/ReanQrCode.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
bool FinderAt(const rean::QrCode& Q, int X0, int Y0)
{
	for (int Y = 0; Y < 7; Y++)
	{
		for (int X = 0; X < 7; X++)
		{
			// Chebyshev distance from the centre: 3 = dark border, 2 = light ring, 0..1 = dark core.
			const int Dx = X > 3 ? X - 3 : 3 - X;
			const int Dy = Y > 3 ? Y - 3 : 3 - Y;
			const bool bWant = (Dx > Dy ? Dx : Dy) != 2;
			if (Q.Dark(X0 + X, Y0 + Y) != bWant) return false;
		}
	}
	return true;
}

void Dump(const rean::QrCode& Q, const std::string& Name, const std::string& Text)
{
	const char* Dir = std::getenv("REAN_QR_DUMP");
	if (!Dir) return;
	// 8 pixels per module, 4-module border, binary PGM; plus the expected text.
	const int Scale = 8;
	const int Border = 4;
	const int W = (Q.Size() + 2 * Border) * Scale;
	const std::string Base = std::string(Dir) + "/" + Name;
	if (FILE* F = std::fopen((Base + ".pgm").c_str(), "wb"))
	{
		std::fprintf(F, "P5\n%d %d\n255\n", W, W);
		for (int Py = 0; Py < W; Py++)
		{
			for (int Px = 0; Px < W; Px++) std::fputc(Q.Dark(Px / Scale - Border, Py / Scale - Border) ? 0 : 255, F);
		}
		std::fclose(F);
	}
	if (FILE* F = std::fopen((Base + ".txt").c_str(), "wb"))
	{
		std::fwrite(Text.data(), 1, Text.size(), F);
		std::fclose(F);
	}
}
} // namespace

TEST_CASE("qr: the device sign-in address fits a small code")
{
	rean::QrCode Q;
	const std::string Url = "https://reanstudios.com/activate?code=BCDF-GHJK";
	REQUIRE(rean::QrCode::Encode(Url, rean::QrCode::Ecc::Medium, Q));
	CHECK(Q.Version() == 4);
	CHECK(Q.Size() == 33);
	CHECK(FinderAt(Q, 0, 0));
	CHECK(FinderAt(Q, Q.Size() - 7, 0));
	CHECK(FinderAt(Q, 0, Q.Size() - 7));
	CHECK(Q.Dark(8, Q.Size() - 8));  // the always-dark module
	for (int I = 8; I < Q.Size() - 8; I++)
	{
		CHECK(Q.Dark(I, 6) == (I % 2 == 0));  // timing patterns
		CHECK(Q.Dark(6, I) == (I % 2 == 0));
	}
	CHECK_FALSE(Q.Dark(-1, 0));
	CHECK_FALSE(Q.Dark(0, Q.Size()));
	Dump(Q, "device_url", Url);
}

TEST_CASE("qr: versions and error-correction levels across sizes")
{
	const rean::QrCode::Ecc Levels[] = { rean::QrCode::Ecc::Low, rean::QrCode::Ecc::Medium, rean::QrCode::Ecc::Quartile, rean::QrCode::Ecc::High };
	const size_t Lengths[] = { 0, 1, 17, 32, 47, 78, 120, 200, 300, 500, 900, 1270 };  // 1273 bytes is the 40-H maximum
	int LastVersion = 0;
	for (int L = 0; L < 4; L++)
	{
		LastVersion = 0;
		for (const size_t Len : Lengths)
		{
			std::string Text;
			for (size_t I = 0; I < Len; I++) Text.push_back(static_cast<char>('A' + (I * 7) % 26));
			rean::QrCode Q;
			REQUIRE(rean::QrCode::Encode(Text, Levels[L], Q));
			CHECK(Q.Size() == Q.Version() * 4 + 17);
			CHECK(Q.Version() >= LastVersion);
			CHECK(Q.Mask() >= 0);
			CHECK(Q.Mask() <= 7);
			LastVersion = Q.Version();
			Dump(Q, "ecc" + std::to_string(L) + "_len" + std::to_string(Len), Text);
		}
	}
	rean::QrCode Big;
	CHECK_FALSE(rean::QrCode::Encode(std::string(3000, 'x'), rean::QrCode::Ecc::Medium, Big));
	CHECK(rean::QrCode::Encode(std::string(2331, 'x'), rean::QrCode::Ecc::Medium, Big));  // version 40-M holds 2331 bytes
	CHECK(Big.Version() == 40);
	CHECK_FALSE(rean::QrCode::Encode(std::string(2332, 'x'), rean::QrCode::Ecc::Medium, Big));
}

TEST_CASE("qr: text rendering has the border")
{
	rean::QrCode Q;
	REQUIRE(rean::QrCode::Encode("REAN", rean::QrCode::Ecc::Low, Q));
	const std::string T = Q.ToText(2);
	CHECK(T.size() == static_cast<size_t>((Q.Size() + 4) * (Q.Size() + 5)));
	CHECK(T.substr(0, static_cast<size_t>(Q.Size() + 4)) == std::string(static_cast<size_t>(Q.Size() + 4), ' '));
}
