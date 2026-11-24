// Copyright (c) REAN Studios. MIT License.
//
// What ReanCore needs from the engine: HTTP, storage for the session, clocks.
// The Unreal plugin implements these with FHttpModule, DPAPI / SaveGame and
// FPlatformTime; tests use fakes.
#pragma once

#include "Rean/ReanExport.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

REAN_CORE_WARNINGS_PUSH

namespace rean
{

struct REAN_CORE_API HttpHeader
{
	std::string name;
	std::string value;
};

struct REAN_CORE_API HttpRequest
{
	std::string method;               // GET, POST, DELETE
	std::string url;
	std::vector<HttpHeader> headers;
	std::string body;                 // UTF-8; empty = no body
};

struct REAN_CORE_API HttpResponse
{
	int status = 0;                   // 0 = network failure (no HTTP answer)
	std::string body;
	std::vector<HttpHeader> headers;  // the SDK reads Date and Retry-After when present

	/** Case-insensitive header lookup; empty when absent. */
	std::string Header(const std::string& Name) const;
};

using HttpDone = std::function<void(HttpResponse)>;

/**
 * Sends HTTPS requests. `Done` must be called exactly once per request, from
 * any thread, at any time (also from inside Send). Answers are queued and
 * handled in Client::Tick, so every SDK callback runs on the thread that
 * calls Tick. A transport may drop pending requests when it is destroyed.
 */
class REAN_CORE_API IHttpTransport
{
public:
	virtual ~IHttpTransport();
	virtual void Send(const HttpRequest& Request, HttpDone Done) = 0;
};

/**
 * Small key/value storage for the refresh token and the event cursor. Treat it
 * like a password store: on Windows the Unreal plugin encrypts with DPAPI; on
 * consoles use the platform's secure save storage.
 */
class REAN_CORE_API IKeyValueStore
{
public:
	virtual ~IKeyValueStore();
	virtual bool Load(const std::string& Key, std::string& OutValue) = 0;
	virtual bool Save(const std::string& Key, const std::string& Value) = 0;
	virtual void Remove(const std::string& Key) = 0;
};

/** Keeps everything in memory (nothing survives a restart). */
class REAN_CORE_API MemoryStore : public IKeyValueStore
{
public:
	bool Load(const std::string& Key, std::string& OutValue) override;
	bool Save(const std::string& Key, const std::string& Value) override;
	void Remove(const std::string& Key) override;
	size_t Count() const { return Values.size(); }

private:
	std::map<std::string, std::string> Values;
};

enum class LogLevel : int
{
	Verbose = 0,
	Info,
	Warning,
	Error,
};

} // namespace rean

REAN_CORE_WARNINGS_POP
