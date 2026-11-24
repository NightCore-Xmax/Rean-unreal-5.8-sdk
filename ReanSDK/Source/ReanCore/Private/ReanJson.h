// Copyright (c) REAN Studios. MIT License.
//
// A small JSON reader/writer for REAN API answers. Locale-independent, no
// exceptions, depth-limited. Missing keys read as null, so lookups chain:
// Json["presence"]["activity"]["party"]["id"].Str().
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rean
{

class Json
{
public:
	enum class Type : unsigned char
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	Json() = default;

	/** Parses UTF-8 JSON. Returns false (and leaves Out null) on any syntax error. */
	static bool Parse(const std::string& Text, Json& Out);

	Type GetType() const { return Kind; }
	bool IsNull() const { return Kind == Type::Null; }
	bool IsObject() const { return Kind == Type::Object; }
	bool IsArray() const { return Kind == Type::Array; }
	bool IsString() const { return Kind == Type::String; }
	bool IsNumber() const { return Kind == Type::Number; }
	bool IsBool() const { return Kind == Type::Bool; }

	/** Member of an object; a shared null value when missing or not an object. */
	const Json& operator[](const char* Key) const;
	const Json& operator[](const std::string& Key) const { return (*this)[Key.c_str()]; }
	bool Has(const char* Key) const;

	/** Array size (0 for non-arrays) and element (null when out of range). */
	size_t Size() const { return Kind == Type::Array ? Items.size() : 0; }
	const Json& At(size_t Index) const;

	std::string Str(const std::string& Fallback = std::string()) const { return Kind == Type::String ? Text : Fallback; }
	double Num(double Fallback) const { return Kind == Type::Number ? Number : Fallback; }
	/** Whole number, or Fallback when not a finite number in int64 range. */
	int64_t Int(int64_t Fallback) const;
	bool Bool(bool Fallback) const { return Kind == Type::Bool ? Flag : Fallback; }

	/** Compact serialization (used to pass unknown event data through). */
	std::string Dump() const;

	static const Json& NullValue();

private:
	friend class JsonParser;
	void DumpTo(std::string& Out) const;

	Type Kind = Type::Null;
	bool Flag = false;
	double Number = 0.0;
	std::string Text;
	std::vector<Json> Items;
	std::vector<std::pair<std::string, Json>> Members;
};

/** Appends S as a JSON string literal (quotes and escapes included). */
void AppendJsonString(std::string& Out, const std::string& S);

/** Builds a JSON object: JsonObjectWriter W; W.Str("a", "x").Int("n", 2); W.Finish(). */
class JsonObjectWriter
{
public:
	JsonObjectWriter& Str(const char* Key, const std::string& Value);
	JsonObjectWriter& Int(const char* Key, int64_t Value);
	JsonObjectWriter& Bool(const char* Key, bool Value);
	std::string Finish();

private:
	void KeyPrefix(const char* Key);
	std::string Out = "{";
	bool bFirst = true;
};

} // namespace rean
