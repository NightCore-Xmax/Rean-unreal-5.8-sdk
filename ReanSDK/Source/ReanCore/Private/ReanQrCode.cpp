// Copyright (c) REAN Studios. MIT License.
//
// QR Code encoder. Algorithm and tables follow Project Nayuki's QR Code
// generator library (MIT License, https://www.nayuki.io/page/qr-code-generator-library).
#include "Rean/ReanQrCode.h"

#include <cstdlib>

namespace rean
{

namespace
{
// Index 0 is unused; rows are Low, Medium, Quartile, High.
const int8_t EccCodewordsPerBlock[4][41] = {
	{ -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 },
	{ -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
};

const int8_t NumErrorCorrectionBlocks[4][41] = {
	{ -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 },
	{ -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },
	{ -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },
	{ -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },
};

constexpr int PenaltyN1 = 3;
constexpr int PenaltyN2 = 3;
constexpr int PenaltyN3 = 40;
constexpr int PenaltyN4 = 10;

int EccIndex(QrCode::Ecc Level) { return static_cast<int>(Level); }

int FormatBitsOf(QrCode::Ecc Level)
{
	switch (Level)
	{
	case QrCode::Ecc::Low: return 1;
	case QrCode::Ecc::Medium: return 0;
	case QrCode::Ecc::Quartile: return 3;
	case QrCode::Ecc::High: return 2;
	}
	return 0;
}

bool BitAt(long X, int I) { return ((X >> I) & 1) != 0; }

int NumRawDataModules(int Ver)
{
	int Result = (16 * Ver + 128) * Ver + 64;
	if (Ver >= 2)
	{
		const int NumAlign = Ver / 7 + 2;
		Result -= (25 * NumAlign - 10) * NumAlign - 55;
		if (Ver >= 7) Result -= 36;
	}
	return Result;
}

int NumDataCodewords(int Ver, QrCode::Ecc Level)
{
	return NumRawDataModules(Ver) / 8 - EccCodewordsPerBlock[EccIndex(Level)][Ver] * NumErrorCorrectionBlocks[EccIndex(Level)][Ver];
}

int ByteModeCountBits(int Ver) { return Ver <= 9 ? 8 : 16; }

uint8_t GfMultiply(uint8_t X, uint8_t Y)
{
	int Z = 0;
	for (int I = 7; I >= 0; I--)
	{
		Z = (Z << 1) ^ ((Z >> 7) * 0x11D);
		Z ^= ((Y >> I) & 1) * X;
	}
	return static_cast<uint8_t>(Z);
}

std::vector<uint8_t> ReedSolomonDivisor(int Degree)
{
	std::vector<uint8_t> Result(static_cast<size_t>(Degree), 0);
	Result.back() = 1;
	uint8_t Root = 1;
	for (int I = 0; I < Degree; I++)
	{
		for (size_t J = 0; J < Result.size(); J++)
		{
			Result[J] = GfMultiply(Result[J], Root);
			if (J + 1 < Result.size()) Result[J] = static_cast<uint8_t>(Result[J] ^ Result[J + 1]);
		}
		Root = GfMultiply(Root, 0x02);
	}
	return Result;
}

std::vector<uint8_t> ReedSolomonRemainder(const std::vector<uint8_t>& Data, const std::vector<uint8_t>& Divisor)
{
	std::vector<uint8_t> Result(Divisor.size(), 0);
	for (const uint8_t B : Data)
	{
		const uint8_t Factor = static_cast<uint8_t>(B ^ Result.front());
		Result.erase(Result.begin());
		Result.push_back(0);
		for (size_t I = 0; I < Result.size(); I++) Result[I] = static_cast<uint8_t>(Result[I] ^ GfMultiply(Divisor[I], Factor));
	}
	return Result;
}

std::vector<int> AlignmentPositions(int Ver, int Size)
{
	if (Ver == 1) return {};
	const int NumAlign = Ver / 7 + 2;
	const int Step = (Ver * 8 + NumAlign * 3 + 5) / (NumAlign * 4 - 4) * 2;
	std::vector<int> Result;
	for (int I = 0, Pos = Size - 7; I < NumAlign - 1; I++, Pos -= Step) Result.insert(Result.begin(), Pos);
	Result.insert(Result.begin(), 6);
	return Result;
}

std::vector<uint8_t> AddEccAndInterleave(const std::vector<uint8_t>& Data, int Ver, QrCode::Ecc Level)
{
	const int NumBlocks = NumErrorCorrectionBlocks[EccIndex(Level)][Ver];
	const int BlockEccLen = EccCodewordsPerBlock[EccIndex(Level)][Ver];
	const int RawCodewords = NumRawDataModules(Ver) / 8;
	const int NumShortBlocks = NumBlocks - RawCodewords % NumBlocks;
	const int ShortBlockLen = RawCodewords / NumBlocks;

	std::vector<std::vector<uint8_t>> Blocks;
	const std::vector<uint8_t> Divisor = ReedSolomonDivisor(BlockEccLen);
	size_t K = 0;
	for (int I = 0; I < NumBlocks; I++)
	{
		const size_t Len = static_cast<size_t>(ShortBlockLen - BlockEccLen + (I < NumShortBlocks ? 0 : 1));
		std::vector<uint8_t> Block(Data.begin() + static_cast<std::ptrdiff_t>(K), Data.begin() + static_cast<std::ptrdiff_t>(K + Len));
		K += Len;
		const std::vector<uint8_t> Ecc = ReedSolomonRemainder(Block, Divisor);
		if (I < NumShortBlocks) Block.push_back(0);
		Block.insert(Block.end(), Ecc.begin(), Ecc.end());
		Blocks.push_back(std::move(Block));
	}
	std::vector<uint8_t> Result;
	for (size_t I = 0; I < Blocks[0].size(); I++)
	{
		for (size_t J = 0; J < Blocks.size(); J++)
		{
			// Skip the padding byte of short blocks.
			if (I != static_cast<size_t>(ShortBlockLen - BlockEccLen) || J >= static_cast<size_t>(NumShortBlocks)) Result.push_back(Blocks[J][I]);
		}
	}
	return Result;
}
} // namespace

bool QrCode::Encode(const std::string& Utf8, Ecc Level, QrCode& Out)
{
	const int Ecl = EccIndex(Level);
	if (Ecl < 0 || Ecl > 3) return false;
	int Ver = 1;
	for (; Ver <= 40; Ver++)
	{
		const long Capacity = static_cast<long>(NumDataCodewords(Ver, Level)) * 8;
		const long Used = 4 + ByteModeCountBits(Ver) + static_cast<long>(Utf8.size()) * 8;
		if (Utf8.size() < (static_cast<size_t>(1) << ByteModeCountBits(Ver)) && Used <= Capacity) break;
	}
	if (Ver > 40) return false;

	// Bit stream: byte-mode segment, terminator, padding.
	std::vector<bool> Bits;
	auto Append = [&Bits](uint32_t Value, int Count) {
		for (int I = Count - 1; I >= 0; I--) Bits.push_back(((Value >> I) & 1u) != 0);
	};
	Append(0x4, 4);
	Append(static_cast<uint32_t>(Utf8.size()), ByteModeCountBits(Ver));
	for (const char C : Utf8) Append(static_cast<unsigned char>(C), 8);
	const size_t CapacityBits = static_cast<size_t>(NumDataCodewords(Ver, Level)) * 8;
	const size_t Terminator = CapacityBits - Bits.size() < 4 ? CapacityBits - Bits.size() : 4;
	Append(0, static_cast<int>(Terminator));
	Append(0, static_cast<int>((8 - Bits.size() % 8) % 8));
	for (uint32_t Pad = 0xEC; Bits.size() < CapacityBits; Pad ^= 0xEC ^ 0x11) Append(Pad, 8);
	std::vector<uint8_t> Data(Bits.size() / 8, 0);
	for (size_t I = 0; I < Bits.size(); I++)
	{
		if (Bits[I]) Data[I >> 3] = static_cast<uint8_t>(Data[I >> 3] | (1 << (7 - static_cast<int>(I & 7))));
	}

	QrCode Q;
	Q.VersionNumber = Ver;
	Q.SideLength = Ver * 4 + 17;
	const size_t Cells = static_cast<size_t>(Q.SideLength) * static_cast<size_t>(Q.SideLength);
	Q.Modules.assign(Cells, 0);
	Q.IsFunction.assign(Cells, 0);
	Q.DrawFunctionPatterns(Level);
	Q.DrawCodewords(AddEccAndInterleave(Data, Ver, Level));

	long Best = -1;
	int BestMask = 0;
	for (int M = 0; M < 8; M++)
	{
		Q.ApplyMask(M);
		Q.DrawFormatBits(Level, M);
		const long Penalty = Q.PenaltyScore();
		if (Best < 0 || Penalty < Best)
		{
			Best = Penalty;
			BestMask = M;
		}
		Q.ApplyMask(M);  // XOR again: undo
	}
	Q.MaskPattern = BestMask;
	Q.ApplyMask(BestMask);
	Q.DrawFormatBits(Level, BestMask);
	Q.IsFunction.clear();
	Out = std::move(Q);
	return true;
}

bool QrCode::Dark(int X, int Y) const
{
	return X >= 0 && Y >= 0 && X < SideLength && Y < SideLength && Get(X, Y);
}

std::string QrCode::ToText(int BorderModules) const
{
	std::string Out;
	for (int Y = -BorderModules; Y < SideLength + BorderModules; Y++)
	{
		for (int X = -BorderModules; X < SideLength + BorderModules; X++) Out.push_back(Dark(X, Y) ? '#' : ' ');
		Out.push_back('\n');
	}
	return Out;
}

void QrCode::Set(int X, int Y, bool bDark)
{
	Modules[static_cast<size_t>(Y) * static_cast<size_t>(SideLength) + static_cast<size_t>(X)] = bDark ? 1 : 0;
}

void QrCode::SetFunction(int X, int Y, bool bDark)
{
	Set(X, Y, bDark);
	IsFunction[static_cast<size_t>(Y) * static_cast<size_t>(SideLength) + static_cast<size_t>(X)] = 1;
}

void QrCode::DrawFunctionPatterns(Ecc Level)
{
	for (int I = 0; I < SideLength; I++)
	{
		SetFunction(6, I, I % 2 == 0);
		SetFunction(I, 6, I % 2 == 0);
	}
	DrawFinder(3, 3);
	DrawFinder(SideLength - 4, 3);
	DrawFinder(3, SideLength - 4);
	const std::vector<int> Align = AlignmentPositions(VersionNumber, SideLength);
	const size_t N = Align.size();
	for (size_t I = 0; I < N; I++)
	{
		for (size_t J = 0; J < N; J++)
		{
			// Not over the three finder patterns.
			if (!((I == 0 && J == 0) || (I == 0 && J == N - 1) || (I == N - 1 && J == 0))) DrawAlignment(Align[I], Align[J]);
		}
	}
	DrawFormatBits(Level, 0);  // reserves the area; redrawn with the real mask later
	DrawVersion();
}

void QrCode::DrawFinder(int X, int Y)
{
	for (int Dy = -4; Dy <= 4; Dy++)
	{
		for (int Dx = -4; Dx <= 4; Dx++)
		{
			const int Dist = std::abs(Dx) > std::abs(Dy) ? std::abs(Dx) : std::abs(Dy);
			const int Xx = X + Dx;
			const int Yy = Y + Dy;
			if (Xx >= 0 && Xx < SideLength && Yy >= 0 && Yy < SideLength) SetFunction(Xx, Yy, Dist != 2 && Dist != 4);
		}
	}
}

void QrCode::DrawAlignment(int X, int Y)
{
	for (int Dy = -2; Dy <= 2; Dy++)
	{
		for (int Dx = -2; Dx <= 2; Dx++)
		{
			const int Dist = std::abs(Dx) > std::abs(Dy) ? std::abs(Dx) : std::abs(Dy);
			SetFunction(X + Dx, Y + Dy, Dist != 1);
		}
	}
}

void QrCode::DrawFormatBits(Ecc Level, int Mask)
{
	const int Data = FormatBitsOf(Level) << 3 | Mask;
	int Rem = Data;
	for (int I = 0; I < 10; I++) Rem = (Rem << 1) ^ ((Rem >> 9) * 0x537);
	const long Bits = static_cast<long>((Data << 10 | Rem) ^ 0x5412);
	for (int I = 0; I <= 5; I++) SetFunction(8, I, BitAt(Bits, I));
	SetFunction(8, 7, BitAt(Bits, 6));
	SetFunction(8, 8, BitAt(Bits, 7));
	SetFunction(7, 8, BitAt(Bits, 8));
	for (int I = 9; I < 15; I++) SetFunction(14 - I, 8, BitAt(Bits, I));
	for (int I = 0; I < 8; I++) SetFunction(SideLength - 1 - I, 8, BitAt(Bits, I));
	for (int I = 8; I < 15; I++) SetFunction(8, SideLength - 15 + I, BitAt(Bits, I));
	SetFunction(8, SideLength - 8, true);  // always dark
}

void QrCode::DrawVersion()
{
	if (VersionNumber < 7) return;
	int Rem = VersionNumber;
	for (int I = 0; I < 12; I++) Rem = (Rem << 1) ^ ((Rem >> 11) * 0x1F25);
	const long Bits = static_cast<long>(VersionNumber) << 12 | Rem;
	for (int I = 0; I < 18; I++)
	{
		const bool B = BitAt(Bits, I);
		const int A = SideLength - 11 + I % 3;
		const int C = I / 3;
		SetFunction(A, C, B);
		SetFunction(C, A, B);
	}
}

void QrCode::DrawCodewords(const std::vector<uint8_t>& Data)
{
	size_t I = 0;
	const size_t TotalBits = Data.size() * 8;
	for (int Right = SideLength - 1; Right >= 1; Right -= 2)
	{
		if (Right == 6) Right = 5;
		for (int Vert = 0; Vert < SideLength; Vert++)
		{
			for (int J = 0; J < 2; J++)
			{
				const int X = Right - J;
				const bool bUpward = ((Right + 1) & 2) == 0;
				const int Y = bUpward ? SideLength - 1 - Vert : Vert;
				if (!Function(X, Y) && I < TotalBits)
				{
					Set(X, Y, ((Data[I >> 3] >> (7 - static_cast<int>(I & 7))) & 1) != 0);
					I++;
				}
			}
		}
	}
}

void QrCode::ApplyMask(int Mask)
{
	for (int Y = 0; Y < SideLength; Y++)
	{
		for (int X = 0; X < SideLength; X++)
		{
			bool bInvert = false;
			switch (Mask)
			{
			case 0: bInvert = (X + Y) % 2 == 0; break;
			case 1: bInvert = Y % 2 == 0; break;
			case 2: bInvert = X % 3 == 0; break;
			case 3: bInvert = (X + Y) % 3 == 0; break;
			case 4: bInvert = (X / 3 + Y / 2) % 2 == 0; break;
			case 5: bInvert = X * Y % 2 + X * Y % 3 == 0; break;
			case 6: bInvert = (X * Y % 2 + X * Y % 3) % 2 == 0; break;
			default: bInvert = ((X + Y) % 2 + X * Y % 3) % 2 == 0; break;
			}
			if (bInvert && !Function(X, Y)) Set(X, Y, !Get(X, Y));
		}
	}
}

void QrCode::FinderAddHistory(int RunLength, int History[7]) const
{
	if (History[0] == 0) RunLength += SideLength;  // light border before the first run
	for (int I = 6; I > 0; I--) History[I] = History[I - 1];
	History[0] = RunLength;
}

int QrCode::FinderCountPatterns(const int History[7]) const
{
	const int N = History[1];
	const bool bCore = N > 0 && History[2] == N && History[3] == N * 3 && History[4] == N && History[5] == N;
	return (bCore && History[0] >= N * 4 && History[6] >= N ? 1 : 0) + (bCore && History[6] >= N * 4 && History[0] >= N ? 1 : 0);
}

int QrCode::FinderTerminateAndCount(bool bRunDark, int RunLength, int History[7]) const
{
	if (bRunDark)
	{
		FinderAddHistory(RunLength, History);
		RunLength = 0;
	}
	RunLength += SideLength;  // light border after the last run
	FinderAddHistory(RunLength, History);
	return FinderCountPatterns(History);
}

long QrCode::PenaltyScore() const
{
	long Result = 0;
	for (int Pass = 0; Pass < 2; Pass++)  // rows, then columns
	{
		for (int A = 0; A < SideLength; A++)
		{
			bool bRunDark = false;
			int Run = 0;
			int History[7] = { 0, 0, 0, 0, 0, 0, 0 };
			for (int B = 0; B < SideLength; B++)
			{
				const bool bDark = Pass == 0 ? Get(B, A) : Get(A, B);
				if (bDark == bRunDark)
				{
					Run++;
					if (Run == 5) Result += PenaltyN1;
					else if (Run > 5) Result++;
				}
				else
				{
					FinderAddHistory(Run, History);
					if (!bRunDark) Result += FinderCountPatterns(History) * PenaltyN3;
					bRunDark = bDark;
					Run = 1;
				}
			}
			Result += FinderTerminateAndCount(bRunDark, Run, History) * PenaltyN3;
		}
	}
	for (int Y = 0; Y < SideLength - 1; Y++)
	{
		for (int X = 0; X < SideLength - 1; X++)
		{
			const bool C = Get(X, Y);
			if (C == Get(X + 1, Y) && C == Get(X, Y + 1) && C == Get(X + 1, Y + 1)) Result += PenaltyN2;
		}
	}
	long DarkCount = 0;
	for (const uint8_t M : Modules) DarkCount += M;
	const long Total = static_cast<long>(SideLength) * SideLength;
	const long K = (std::labs(DarkCount * 20 - Total * 10) + Total - 1) / Total - 1;
	Result += K * PenaltyN4;
	return Result;
}

} // namespace rean
