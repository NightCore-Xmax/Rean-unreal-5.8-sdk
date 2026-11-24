// Copyright (c) REAN Studios. MIT License.
#include "ReanText.h"

#include <cstring>

namespace rean
{

namespace
{
int HexDigitValue(char C)
{
	if (C >= '0' && C <= '9') return C - '0';
	if (C >= 'a' && C <= 'f') return C - 'a' + 10;
	if (C >= 'A' && C <= 'F') return C - 'A' + 10;
	return -1;
}

bool IsDigit(char C) { return C >= '0' && C <= '9'; }

// Reads exactly Count digits at Text[Pos]; advances Pos.
bool ReadDigits(const std::string& Text, size_t& Pos, int Count, int& Out)
{
	int V = 0;
	for (int I = 0; I < Count; I++)
	{
		if (Pos >= Text.size() || !IsDigit(Text[Pos])) return false;
		V = V * 10 + (Text[Pos] - '0');
		Pos++;
	}
	Out = V;
	return true;
}

bool ValidTime(int Month, int Day, int Hour, int Minute, int Second)
{
	return Month >= 1 && Month <= 12 && Day >= 1 && Day <= 31 && Hour >= 0 && Hour <= 23 && Minute >= 0 && Minute <= 59 &&
	       Second >= 0 && Second <= 60;
}

int64_t ToUnixMs(int Year, int Month, int Day, int Hour, int Minute, int Second, int Millis)
{
	const int64_t Days = DaysFromCivil(Year, static_cast<unsigned>(Month), static_cast<unsigned>(Day));
	return ((Days * 24 + Hour) * 60 + Minute) * 60000 + static_cast<int64_t>(Second) * 1000 + Millis;
}
} // namespace

std::string UrlEncode(const std::string& Text)
{
	static const char* const Hex = "0123456789ABCDEF";
	std::string Out;
	Out.reserve(Text.size() * 3);
	for (const char C : Text)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		if ((U >= 'A' && U <= 'Z') || (U >= 'a' && U <= 'z') || (U >= '0' && U <= '9') || U == '-' || U == '.' || U == '_' || U == '~')
		{
			Out.push_back(C);
		}
		else
		{
			Out.push_back('%');
			Out.push_back(Hex[U >> 4]);
			Out.push_back(Hex[U & 0xF]);
		}
	}
	return Out;
}

std::string UrlDecode(const std::string& Text)
{
	std::string Out;
	Out.reserve(Text.size());
	for (size_t I = 0; I < Text.size(); I++)
	{
		const char C = Text[I];
		if (C == '+')
		{
			Out.push_back(' ');
		}
		else if (C == '%' && I + 2 < Text.size())
		{
			const int Hi = HexDigitValue(Text[I + 1]);
			const int Lo = HexDigitValue(Text[I + 2]);
			if (Hi >= 0 && Lo >= 0)
			{
				Out.push_back(static_cast<char>((Hi << 4) | Lo));
				I += 2;
			}
			else
			{
				Out.push_back(C);
			}
		}
		else
		{
			Out.push_back(C);
		}
	}
	return Out;
}

std::string FormEncode(const KeyValues& Fields)
{
	std::string Out;
	for (const auto& F : Fields)
	{
		if (!Out.empty()) Out.push_back('&');
		Out += UrlEncode(F.first);
		Out.push_back('=');
		Out += UrlEncode(F.second);
	}
	return Out;
}

KeyValues ParseQuery(const std::string& UrlOrQuery)
{
	std::string Query = UrlOrQuery;
	const size_t Hash = Query.find('#');
	if (Hash != std::string::npos) Query.erase(Hash);
	const size_t Question = Query.find('?');
	if (Question != std::string::npos)
	{
		Query.erase(0, Question + 1);
	}
	else if (Query.find('=') == std::string::npos || Query.find("://") != std::string::npos)
	{
		return {};  // a URL without a query, or no parameters at all
	}
	KeyValues Out;
	size_t Start = 0;
	while (Start <= Query.size())
	{
		size_t End = Query.find('&', Start);
		if (End == std::string::npos) End = Query.size();
		const std::string Part = Query.substr(Start, End - Start);
		if (!Part.empty())
		{
			const size_t Eq = Part.find('=');
			if (Eq == std::string::npos) Out.emplace_back(UrlDecode(Part), std::string());
			else Out.emplace_back(UrlDecode(Part.substr(0, Eq)), UrlDecode(Part.substr(Eq + 1)));
		}
		Start = End + 1;
	}
	return Out;
}

bool FindParam(const KeyValues& Params, const char* Key, std::string& OutValue)
{
	for (const auto& P : Params)
	{
		if (P.first == Key)
		{
			OutValue = P.second;
			return true;
		}
	}
	return false;
}

std::string ToLowerAscii(std::string Text)
{
	for (char& C : Text)
	{
		if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');
	}
	return Text;
}

bool IsMachineCode(const std::string& Text)
{
	if (Text.empty() || Text.size() > 64 || Text[0] < 'a' || Text[0] > 'z') return false;
	for (const char C : Text)
	{
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '.')) return false;
	}
	return true;
}

int64_t DaysFromCivil(int64_t Year, unsigned Month, unsigned Day)
{
	// Howard Hinnant's days_from_civil.
	Year -= Month <= 2 ? 1 : 0;
	const int64_t Era = (Year >= 0 ? Year : Year - 399) / 400;
	const unsigned Yoe = static_cast<unsigned>(Year - Era * 400);
	const unsigned Doy = (153 * (Month > 2 ? Month - 3 : Month + 9) + 2) / 5 + Day - 1;
	const unsigned Doe = Yoe * 365 + Yoe / 4 - Yoe / 100 + Doy;
	return Era * 146097 + static_cast<int64_t>(Doe) - 719468;
}

bool ParseIso8601Ms(const std::string& Text, int64_t& OutUnixMs)
{
	size_t Pos = 0;
	int Year = 0, Month = 0, Day = 0, Hour = 0, Minute = 0, Second = 0;
	if (!ReadDigits(Text, Pos, 4, Year) || Pos >= Text.size() || Text[Pos++] != '-') return false;
	if (!ReadDigits(Text, Pos, 2, Month) || Pos >= Text.size() || Text[Pos++] != '-') return false;
	if (!ReadDigits(Text, Pos, 2, Day)) return false;
	if (Pos >= Text.size() || (Text[Pos] != 'T' && Text[Pos] != 't' && Text[Pos] != ' ')) return false;
	Pos++;
	if (!ReadDigits(Text, Pos, 2, Hour) || Pos >= Text.size() || Text[Pos++] != ':') return false;
	if (!ReadDigits(Text, Pos, 2, Minute)) return false;
	if (Pos < Text.size() && Text[Pos] == ':')
	{
		Pos++;
		if (!ReadDigits(Text, Pos, 2, Second)) return false;
	}
	int Millis = 0;
	if (Pos < Text.size() && (Text[Pos] == '.' || Text[Pos] == ','))
	{
		Pos++;
		int Scale = 100;
		bool bAny = false;
		while (Pos < Text.size() && IsDigit(Text[Pos]))
		{
			Millis += (Text[Pos] - '0') * Scale;
			Scale /= 10;
			Pos++;
			bAny = true;
		}
		if (!bAny) return false;
	}
	if (!ValidTime(Month, Day, Hour, Minute, Second)) return false;
	int64_t Offset = 0;
	if (Pos < Text.size())
	{
		const char Z = Text[Pos];
		if (Z == 'Z' || Z == 'z')
		{
			Pos++;
		}
		else if (Z == '+' || Z == '-')
		{
			Pos++;
			int OffH = 0, OffM = 0;
			if (!ReadDigits(Text, Pos, 2, OffH)) return false;
			if (Pos < Text.size() && Text[Pos] == ':') Pos++;
			if (Pos < Text.size() && !ReadDigits(Text, Pos, 2, OffM)) return false;
			if (OffH > 23 || OffM > 59) return false;
			Offset = (static_cast<int64_t>(OffH) * 60 + OffM) * 60000;
			if (Z == '-') Offset = -Offset;
		}
		else
		{
			return false;
		}
	}
	if (Pos != Text.size()) return false;
	OutUnixMs = ToUnixMs(Year, Month, Day, Hour, Minute, Second, Millis) - Offset;
	return true;
}

bool ParseHttpDateMs(const std::string& Text, int64_t& OutUnixMs)
{
	// "Sun, 06 Nov 1994 08:49:37 GMT"
	static const char* const Months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	const size_t Comma = Text.find(", ");
	if (Comma == std::string::npos) return false;
	size_t Pos = Comma + 2;
	int Day = 0, Year = 0, Hour = 0, Minute = 0, Second = 0;
	if (!ReadDigits(Text, Pos, 2, Day) || Pos >= Text.size() || Text[Pos++] != ' ') return false;
	if (Pos + 4 > Text.size()) return false;
	int Month = 0;
	for (int I = 0; I < 12; I++)
	{
		if (Text.compare(Pos, 3, Months[I]) == 0) Month = I + 1;
	}
	if (Month == 0 || Text[Pos + 3] != ' ') return false;
	Pos += 4;
	if (!ReadDigits(Text, Pos, 4, Year) || Pos >= Text.size() || Text[Pos++] != ' ') return false;
	if (!ReadDigits(Text, Pos, 2, Hour) || Pos >= Text.size() || Text[Pos++] != ':') return false;
	if (!ReadDigits(Text, Pos, 2, Minute) || Pos >= Text.size() || Text[Pos++] != ':') return false;
	if (!ReadDigits(Text, Pos, 2, Second)) return false;
	if (Text.compare(Pos, std::string::npos, " GMT") != 0) return false;
	if (!ValidTime(Month, Day, Hour, Minute, Second)) return false;
	OutUnixMs = ToUnixMs(Year, Month, Day, Hour, Minute, Second, 0);
	return true;
}

} // namespace rean
