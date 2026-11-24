// Copyright (c) REAN Studios. MIT License.
//
// SHA-256 (for PKCE S256) and base64url. Nothing here is secret-sensitive
// beyond PKCE, so a straightforward portable implementation is enough.
#pragma once

#include <cstdint>
#include <string>

namespace rean
{

/** 32-byte SHA-256 digest of Data. */
std::string Sha256(const std::string& Data);

/** base64url without padding (RFC 4648 §5). */
std::string Base64UrlEncode(const std::string& Bytes);

/** Decodes base64url (padding optional; '+' and '/' also accepted). False on bad input. */
bool Base64UrlDecode(const std::string& Text, std::string& OutBytes);

/** PKCE S256 challenge: base64url(SHA-256(verifier)). */
std::string PkceChallenge(const std::string& Verifier);

} // namespace rean
