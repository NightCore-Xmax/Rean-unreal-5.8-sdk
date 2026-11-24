// Copyright (c) REAN Studios. MIT License.
#include "ReanJson.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rean
{

namespace
{
constexpr int MaxJsonDepth = 64;

void AppendUtf8(std::string& Out, uint32_t Cp)
{
	if (Cp < 0x80)
	{
		Out.push_back(static_cast<char>(Cp));
	}
	else if (Cp < 0x800)
	{
		Out.push_back(static_cast<char>(0xC0 | (Cp >> 6)));
		Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
	}
	else if (Cp < 0x10000)
	{
		Out.push_back(static_cast<char>(0xE0 | (Cp >> 12)));
		Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
		Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
	}
	else
	{
		Out.push_back(static_cast<char>(0xF0 | (Cp >> 18)));
		Out.push_back(static_cast<char>(0x80 | ((Cp >> 12) & 0x3F)));
		Out.push_back(static_cast<char>(0x80 | ((Cp >> 6) & 0x3F)));
		Out.push_back(static_cast<char>(0x80 | (Cp & 0x3F)));
	}
}

int HexValue(char C)
{
	if (C >= '0' && C <= '9') return C - '0';
	if (C >= 'a' && C <= 'f') return C - 'a' + 10;
	if (C >= 'A' && C <= 'F') return C - 'A' + 10;
	return -1;
}
} // namespace

class JsonParser
{
public:
	explicit JsonParser(const std::string& InText) : S(InText) {}

	bool Run(Json& Out)
	{
		SkipSpace();
		if (!Value(Out, 0)) return false;
		SkipSpace();
		return Pos == S.size();
	}

private:
	const std::string& S;
	size_t Pos = 0;

	void SkipSpace()
	{
		while (Pos < S.size() && (S[Pos] == ' ' || S[Pos] == '\t' || S[Pos] == '\n' || S[Pos] == '\r')) Pos++;
	}

	bool Literal(const char* Word)
	{
		const size_t Len = std::strlen(Word);
		if (S.compare(Pos, Len, Word) != 0) return false;
		Pos += Len;
		return true;
	}

	bool Value(Json& Out, int Depth)
	{
		if (Depth > MaxJsonDepth || Pos >= S.size()) return false;
		const char C = S[Pos];
		if (C == '{') return ObjectValue(Out, Depth);
		if (C == '[') return ArrayValue(Out, Depth);
		if (C == '"')
		{
			Out.Kind = Json::Type::String;
			return StringValue(Out.Text);
		}
		if (C == 't')
		{
			Out.Kind = Json::Type::Bool;
			Out.Flag = true;
			return Literal("true");
		}
		if (C == 'f')
		{
			Out.Kind = Json::Type::Bool;
			Out.Flag = false;
			return Literal("false");
		}
		if (C == 'n')
		{
			Out.Kind = Json::Type::Null;
			return Literal("null");
		}
		return NumberValue(Out);
	}

	bool ObjectValue(Json& Out, int Depth)
	{
		Out.Kind = Json::Type::Object;
		Pos++;  // {
		SkipSpace();
		if (Pos < S.size() && S[Pos] == '}')
		{
			Pos++;
			return true;
		}
		for (;;)
		{
			SkipSpace();
			if (Pos >= S.size() || S[Pos] != '"') return false;
			std::string Key;
			if (!StringValue(Key)) return false;
			SkipSpace();
			if (Pos >= S.size() || S[Pos] != ':') return false;
			Pos++;
			SkipSpace();
			Json Member;
			if (!Value(Member, Depth + 1)) return false;
			Out.Members.emplace_back(std::move(Key), std::move(Member));
			SkipSpace();
			if (Pos >= S.size()) return false;
			if (S[Pos] == ',')
			{
				Pos++;
				continue;
			}
			if (S[Pos] == '}')
			{
				Pos++;
				return true;
			}
			return false;
		}
	}

	bool ArrayValue(Json& Out, int Depth)
	{
		Out.Kind = Json::Type::Array;
		Pos++;  // [
		SkipSpace();
		if (Pos < S.size() && S[Pos] == ']')
		{
			Pos++;
			return true;
		}
		for (;;)
		{
			SkipSpace();
			Json Item;
			if (!Value(Item, Depth + 1)) return false;
			Out.Items.push_back(std::move(Item));
			SkipSpace();
			if (Pos >= S.size()) return false;
			if (S[Pos] == ',')
			{
				Pos++;
				continue;
			}
			if (S[Pos] == ']')
			{
				Pos++;
				return true;
			}
			return false;
		}
	}

	bool Hex4(uint32_t& Out)
	{
		if (Pos + 4 > S.size()) return false;
		uint32_t V = 0;
		for (int I = 0; I < 4; I++)
		{
			const int H = HexValue(S[Pos + static_cast<size_t>(I)]);
			if (H < 0) return false;
			V = (V << 4) | static_cast<uint32_t>(H);
		}
		Pos += 4;
		Out = V;
		return true;
	}

	bool StringValue(std::string& Out)
	{
		Pos++;  // opening quote
		Out.clear();
		while (Pos < S.size())
		{
			const char C = S[Pos++];
			if (C == '"') return true;
			if (static_cast<unsigned char>(C) < 0x20) return false;
			if (C != '\\')
			{
				Out.push_back(C);
				continue;
			}
			if (Pos >= S.size()) return false;
			const char E = S[Pos++];
			switch (E)
			{
			case '"': Out.push_back('"'); break;
			case '\\': Out.push_back('\\'); break;
			case '/': Out.push_back('/'); break;
			case 'b': Out.push_back('\b'); break;
			case 'f': Out.push_back('\f'); break;
			case 'n': Out.push_back('\n'); break;
			case 'r': Out.push_back('\r'); break;
			case 't': Out.push_back('\t'); break;
			case 'u':
			{
				uint32_t Cp = 0;
				if (!Hex4(Cp)) return false;
				if (Cp >= 0xD800 && Cp <= 0xDBFF)
				{
					// High surrogate: a low surrogate must follow, else U+FFFD.
					uint32_t Low = 0;
					const size_t Save = Pos;
					if (Pos + 1 < S.size() && S[Pos] == '\\' && S[Pos + 1] == 'u')
					{
						Pos += 2;
						if (Hex4(Low) && Low >= 0xDC00 && Low <= 0xDFFF)
						{
							Cp = 0x10000 + ((Cp - 0xD800) << 10) + (Low - 0xDC00);
						}
						else
						{
							Pos = Save;
							Cp = 0xFFFD;
						}
					}
					else
					{
						Cp = 0xFFFD;
					}
				}
				else if (Cp >= 0xDC00 && Cp <= 0xDFFF)
				{
					Cp = 0xFFFD;
				}
				AppendUtf8(Out, Cp);
				break;
			}
			default:
				return false;
			}
		}
		return false;
	}

	// Locale-independent number parsing (strtod depends on LC_NUMERIC).
	bool NumberValue(Json& Out)
	{
		const size_t Start = Pos;
		bool bNegative = false;
		if (Pos < S.size() && S[Pos] == '-')
		{
			bNegative = true;
			Pos++;
		}
		if (Pos >= S.size() || S[Pos] < '0' || S[Pos] > '9') return false;
		if (S[Pos] == '0' && Pos + 1 < S.size() && S[Pos + 1] >= '0' && S[Pos + 1] <= '9') return false;
		uint64_t Mantissa = 0;
		int DecimalExponent = 0;
		int Digits = 0;
		while (Pos < S.size() && S[Pos] >= '0' && S[Pos] <= '9')
		{
			if (Digits < 19)
			{
				Mantissa = Mantissa * 10 + static_cast<uint64_t>(S[Pos] - '0');
				if (Mantissa != 0) Digits++;
			}
			else
			{
				DecimalExponent++;
			}
			Pos++;
		}
		if (Pos < S.size() && S[Pos] == '.')
		{
			Pos++;
			if (Pos >= S.size() || S[Pos] < '0' || S[Pos] > '9') return false;
			while (Pos < S.size() && S[Pos] >= '0' && S[Pos] <= '9')
			{
				if (Digits < 19)
				{
					Mantissa = Mantissa * 10 + static_cast<uint64_t>(S[Pos] - '0');
					if (Mantissa != 0) Digits++;
					DecimalExponent--;
				}
				Pos++;
			}
		}
		if (Pos < S.size() && (S[Pos] == 'e' || S[Pos] == 'E'))
		{
			Pos++;
			bool bExpNegative = false;
			if (Pos < S.size() && (S[Pos] == '+' || S[Pos] == '-'))
			{
				bExpNegative = S[Pos] == '-';
				Pos++;
			}
			if (Pos >= S.size() || S[Pos] < '0' || S[Pos] > '9') return false;
			int Exp = 0;
			while (Pos < S.size() && S[Pos] >= '0' && S[Pos] <= '9')
			{
				if (Exp < 10000) Exp = Exp * 10 + (S[Pos] - '0');
				Pos++;
			}
			DecimalExponent += bExpNegative ? -Exp : Exp;
		}
		if (Pos == Start) return false;
		double V = static_cast<double>(Mantissa);
		if (DecimalExponent != 0) V *= std::pow(10.0, static_cast<double>(DecimalExponent));
		Out.Kind = Json::Type::Number;
		Out.Number = bNegative ? -V : V;
		return true;
	}
};

bool Json::Parse(const std::string& Text, Json& Out)
{
	Json Result;
	JsonParser Parser(Text);
	if (!Parser.Run(Result))
	{
		Out = Json();
		return false;
	}
	Out = std::move(Result);
	return true;
}

const Json& Json::NullValue()
{
	static const Json Null;
	return Null;
}

const Json& Json::operator[](const char* Key) const
{
	if (Kind == Type::Object)
	{
		for (const auto& M : Members)
		{
			if (M.first == Key) return M.second;
		}
	}
	return NullValue();
}

bool Json::Has(const char* Key) const
{
	if (Kind != Type::Object) return false;
	for (const auto& M : Members)
	{
		if (M.first == Key) return true;
	}
	return false;
}

const Json& Json::At(size_t Index) const
{
	return (Kind == Type::Array && Index < Items.size()) ? Items[Index] : NullValue();
}

int64_t Json::Int(int64_t Fallback) const
{
	if (Kind != Type::Number || !std::isfinite(Number)) return Fallback;
	if (Number >= 9.2e18 || Number <= -9.2e18) return Fallback;
	return static_cast<int64_t>(Number);
}

void AppendJsonString(std::string& Out, const std::string& S)
{
	static const char* const HexDigits = "0123456789abcdef";
	Out.push_back('"');
	for (const char C : S)
	{
		const unsigned char U = static_cast<unsigned char>(C);
		switch (C)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		case '\b': Out += "\\b"; break;
		case '\f': Out += "\\f"; break;
		default:
			if (U < 0x20)
			{
				Out += "\\u00";
				Out.push_back(HexDigits[U >> 4]);
				Out.push_back(HexDigits[U & 0xF]);
			}
			else
			{
				Out.push_back(C);
			}
		}
	}
	Out.push_back('"');
}

void Json::DumpTo(std::string& Out) const
{
	switch (Kind)
	{
	case Type::Null: Out += "null"; break;
	case Type::Bool: Out += Flag ? "true" : "false"; break;
	case Type::Number:
	{
		char Buf[40];
		if (!std::isfinite(Number))
		{
			Out += "null";
		}
		else if (Number == std::floor(Number) && std::fabs(Number) < 9.0e15)
		{
			std::snprintf(Buf, sizeof Buf, "%lld", static_cast<long long>(Number));
			Out += Buf;
		}
		else
		{
			std::snprintf(Buf, sizeof Buf, "%.17g", Number);
			for (char* P = Buf; *P; ++P)
			{
				if (*P == ',') *P = '.';  // locales with a decimal comma
			}
			Out += Buf;
		}
		break;
	}
	case Type::String: AppendJsonString(Out, Text); break;
	case Type::Array:
		Out.push_back('[');
		for (size_t I = 0; I < Items.size(); I++)
		{
			if (I) Out.push_back(',');
			Items[I].DumpTo(Out);
		}
		Out.push_back(']');
		break;
	case Type::Object:
		Out.push_back('{');
		for (size_t I = 0; I < Members.size(); I++)
		{
			if (I) Out.push_back(',');
			AppendJsonString(Out, Members[I].first);
			Out.push_back(':');
			Members[I].second.DumpTo(Out);
		}
		Out.push_back('}');
		break;
	}
}

std::string Json::Dump() const
{
	std::string Out;
	DumpTo(Out);
	return Out;
}

void JsonObjectWriter::KeyPrefix(const char* Key)
{
	if (!bFirst) Out.push_back(',');
	bFirst = false;
	AppendJsonString(Out, Key);
	Out.push_back(':');
}

JsonObjectWriter& JsonObjectWriter::Str(const char* Key, const std::string& Value)
{
	KeyPrefix(Key);
	AppendJsonString(Out, Value);
	return *this;
}

JsonObjectWriter& JsonObjectWriter::Int(const char* Key, int64_t Value)
{
	KeyPrefix(Key);
	Out += std::to_string(Value);
	return *this;
}

JsonObjectWriter& JsonObjectWriter::Bool(const char* Key, bool Value)
{
	KeyPrefix(Key);
	Out += Value ? "true" : "false";
	return *this;
}

std::string JsonObjectWriter::Finish()
{
	Out.push_back('}');
	return Out;
}

} // namespace rean
