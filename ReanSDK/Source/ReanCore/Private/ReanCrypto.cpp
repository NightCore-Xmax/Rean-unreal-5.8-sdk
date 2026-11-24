// Copyright (c) REAN Studios. MIT License.
#include "ReanCrypto.h"

namespace rean
{

namespace
{
constexpr uint32_t ShaK[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t RotR(uint32_t X, uint32_t N) { return (X >> N) | (X << (32u - N)); }

void ShaBlock(uint32_t State[8], const unsigned char* Block)
{
	uint32_t W[64];
	for (int I = 0; I < 16; I++)
	{
		W[I] = (static_cast<uint32_t>(Block[I * 4]) << 24) | (static_cast<uint32_t>(Block[I * 4 + 1]) << 16) |
		       (static_cast<uint32_t>(Block[I * 4 + 2]) << 8) | static_cast<uint32_t>(Block[I * 4 + 3]);
	}
	for (int I = 16; I < 64; I++)
	{
		const uint32_t S0 = RotR(W[I - 15], 7) ^ RotR(W[I - 15], 18) ^ (W[I - 15] >> 3);
		const uint32_t S1 = RotR(W[I - 2], 17) ^ RotR(W[I - 2], 19) ^ (W[I - 2] >> 10);
		W[I] = W[I - 16] + S0 + W[I - 7] + S1;
	}
	uint32_t A = State[0], B = State[1], C = State[2], D = State[3];
	uint32_t E = State[4], F = State[5], G = State[6], H = State[7];
	for (int I = 0; I < 64; I++)
	{
		const uint32_t S1 = RotR(E, 6) ^ RotR(E, 11) ^ RotR(E, 25);
		const uint32_t Ch = (E & F) ^ (~E & G);
		const uint32_t T1 = H + S1 + Ch + ShaK[I] + W[I];
		const uint32_t S0 = RotR(A, 2) ^ RotR(A, 13) ^ RotR(A, 22);
		const uint32_t Maj = (A & B) ^ (A & C) ^ (B & C);
		const uint32_t T2 = S0 + Maj;
		H = G;
		G = F;
		F = E;
		E = D + T1;
		D = C;
		C = B;
		B = A;
		A = T1 + T2;
	}
	State[0] += A;
	State[1] += B;
	State[2] += C;
	State[3] += D;
	State[4] += E;
	State[5] += F;
	State[6] += G;
	State[7] += H;
}

const char* const Base64UrlAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int Base64Value(char C)
{
	if (C >= 'A' && C <= 'Z') return C - 'A';
	if (C >= 'a' && C <= 'z') return C - 'a' + 26;
	if (C >= '0' && C <= '9') return C - '0' + 52;
	if (C == '-' || C == '+') return 62;
	if (C == '_' || C == '/') return 63;
	return -1;
}
} // namespace

std::string Sha256(const std::string& Data)
{
	uint32_t State[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
	const unsigned char* Bytes = reinterpret_cast<const unsigned char*>(Data.data());
	const size_t Len = Data.size();
	size_t Offset = 0;
	while (Len - Offset >= 64)
	{
		ShaBlock(State, Bytes + Offset);
		Offset += 64;
	}
	unsigned char Tail[128] = {};
	const size_t Rest = Len - Offset;
	for (size_t I = 0; I < Rest; I++) Tail[I] = Bytes[Offset + I];
	Tail[Rest] = 0x80;
	const size_t TailLen = (Rest < 56) ? 64 : 128;
	const uint64_t Bits = static_cast<uint64_t>(Len) * 8u;
	for (int I = 0; I < 8; I++) Tail[TailLen - 1 - static_cast<size_t>(I)] = static_cast<unsigned char>(Bits >> (8 * I));
	ShaBlock(State, Tail);
	if (TailLen == 128) ShaBlock(State, Tail + 64);
	std::string Out(32, '\0');
	for (int I = 0; I < 8; I++)
	{
		Out[static_cast<size_t>(I) * 4] = static_cast<char>(State[I] >> 24);
		Out[static_cast<size_t>(I) * 4 + 1] = static_cast<char>(State[I] >> 16);
		Out[static_cast<size_t>(I) * 4 + 2] = static_cast<char>(State[I] >> 8);
		Out[static_cast<size_t>(I) * 4 + 3] = static_cast<char>(State[I]);
	}
	return Out;
}

std::string Base64UrlEncode(const std::string& Bytes)
{
	std::string Out;
	Out.reserve((Bytes.size() * 4 + 2) / 3);
	uint32_t Acc = 0;
	int Bits = 0;
	for (const char C : Bytes)
	{
		Acc = (Acc << 8) | static_cast<unsigned char>(C);
		Bits += 8;
		while (Bits >= 6)
		{
			Bits -= 6;
			Out.push_back(Base64UrlAlphabet[(Acc >> Bits) & 0x3F]);
		}
	}
	if (Bits > 0) Out.push_back(Base64UrlAlphabet[(Acc << (6 - Bits)) & 0x3F]);
	return Out;
}

bool Base64UrlDecode(const std::string& Text, std::string& OutBytes)
{
	OutBytes.clear();
	uint32_t Acc = 0;
	int Bits = 0;
	size_t Used = 0;
	for (const char C : Text)
	{
		if (C == '=') break;
		const int V = Base64Value(C);
		if (V < 0) return false;
		Acc = ((Acc << 6) | static_cast<uint32_t>(V)) & 0xFFFFFFu;
		Bits += 6;
		Used++;
		if (Bits >= 8)
		{
			Bits -= 8;
			OutBytes.push_back(static_cast<char>((Acc >> Bits) & 0xFF));
		}
	}
	return Used % 4 != 1;  // one leftover character cannot encode a byte
}

std::string PkceChallenge(const std::string& Verifier)
{
	return Base64UrlEncode(Sha256(Verifier));
}

} // namespace rean
