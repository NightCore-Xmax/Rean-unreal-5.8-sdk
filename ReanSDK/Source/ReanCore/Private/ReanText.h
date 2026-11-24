// Copyright (c) REAN Studios. MIT License.
//
// URL encoding, query parsing and time parsing. Locale-independent.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rean
{

using KeyValues = std::vector<std::pair<std::string, std::string>>;

/** Percent-encodes everything except RFC 3986 unreserved characters. */
std::string UrlEncode(const std::string& Text);
/** Decodes %XX and '+' (as space). Invalid escapes are kept as they are. */
std::string UrlDecode(const std::string& Text);
/** application/x-www-form-urlencoded body (or query string) from ordered pairs. */
std::string FormEncode(const KeyValues& Fields);
/** Query parameters of a full URL ("http://h/p?a=1#f") or of a bare query ("a=1&b=2" or "?a=1"). */
KeyValues ParseQuery(const std::string& UrlOrQuery);
/** First value of Key in Params; false when absent. */
bool FindParam(const KeyValues& Params, const char* Key, std::string& OutValue);

std::string ToLowerAscii(std::string Text);
/** A lower-case machine code such as "invalid_grant" (not a sentence). */
bool IsMachineCode(const std::string& Text);

/** ISO 8601 / RFC 3339 date-time ("2026-11-22T20:15:00.123Z", "+00:00" offsets, ' ' instead of 'T'). */
bool ParseIso8601Ms(const std::string& Text, int64_t& OutUnixMs);
/** RFC 7231 IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT"). */
bool ParseHttpDateMs(const std::string& Text, int64_t& OutUnixMs);
/** Unix days from a civil date (proleptic Gregorian). */
int64_t DaysFromCivil(int64_t Year, unsigned Month, unsigned Day);

} // namespace rean
