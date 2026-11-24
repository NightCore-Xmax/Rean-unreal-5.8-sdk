// Copyright (c) REAN Studios. MIT License.
//
// QR Code encoder (ISO/IEC 18004, byte mode) for showing the device sign-in
// address (verification_uri_complete) as a code players scan with a phone.
// Structure follows Project Nayuki's QR Code generator (MIT), rewritten
// without exceptions so it builds inside Unreal.
#pragma once

#include "Rean/ReanExport.h"

#include <cstdint>
#include <string>
#include <vector>

REAN_CORE_WARNINGS_PUSH

namespace rean
{

class REAN_CORE_API QrCode
{
public:
	enum class Ecc : int
	{
		Low = 0,      // ~7% of the code can be damaged
		Medium = 1,   // ~15% (default)
		Quartile = 2, // ~25%
		High = 3,     // ~30%
	};

	/** Encodes UTF-8 text in the smallest version (1..40) that fits. False if it is too long. */
	static bool Encode(const std::string& Utf8, Ecc Level, QrCode& Out);

	int Version() const { return VersionNumber; }
	/** Width and height in modules (21..177). Draw a 4-module light border around it. */
	int Size() const { return SideLength; }
	/** True for a dark module. Out-of-range coordinates are light. */
	bool Dark(int X, int Y) const;
	/** The mask pattern that was chosen (0..7). */
	int Mask() const { return MaskPattern; }

	/** Rows of '#' (dark) and ' ' (light) with a border of BorderModules (for logs and tests). */
	std::string ToText(int BorderModules = 4) const;

private:
	int VersionNumber = 0;
	int SideLength = 0;
	int MaskPattern = 0;
	std::vector<uint8_t> Modules;
	std::vector<uint8_t> IsFunction;

	void Set(int X, int Y, bool bDark);
	void SetFunction(int X, int Y, bool bDark);
	bool Get(int X, int Y) const { return Modules[static_cast<size_t>(Y) * static_cast<size_t>(SideLength) + static_cast<size_t>(X)] != 0; }
	bool Function(int X, int Y) const { return IsFunction[static_cast<size_t>(Y) * static_cast<size_t>(SideLength) + static_cast<size_t>(X)] != 0; }
	void DrawFunctionPatterns(Ecc Level);
	void DrawFinder(int X, int Y);
	void DrawAlignment(int X, int Y);
	void DrawFormatBits(Ecc Level, int Mask);
	void DrawVersion();
	void DrawCodewords(const std::vector<uint8_t>& Data);
	void ApplyMask(int Mask);
	long PenaltyScore() const;
	int FinderCountPatterns(const int History[7]) const;
	void FinderAddHistory(int RunLength, int History[7]) const;
	int FinderTerminateAndCount(bool bRunDark, int RunLength, int History[7]) const;
};

} // namespace rean

REAN_CORE_WARNINGS_POP
