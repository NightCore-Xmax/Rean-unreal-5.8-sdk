// Copyright (c) REAN Studios. MIT License.
#include "curl_transport.h"

#include <curl/curl.h>

#include <cstdio>
#include <string>

namespace reantools
{

namespace
{
size_t CollectBody(char* Data, size_t Size, size_t Count, void* User)
{
	static_cast<std::string*>(User)->append(Data, Size * Count);
	return Size * Count;
}

size_t CollectHeader(char* Data, size_t Size, size_t Count, void* User)
{
	auto* Headers = static_cast<std::vector<rean::HttpHeader>*>(User);
	std::string Line(Data, Size * Count);
	const size_t Colon = Line.find(':');
	if (Colon != std::string::npos)
	{
		std::string Value = Line.substr(Colon + 1);
		while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t')) Value.erase(0, 1);
		while (!Value.empty() && (Value.back() == '\r' || Value.back() == '\n' || Value.back() == ' ')) Value.pop_back();
		Headers->push_back({ Line.substr(0, Colon), Value });
	}
	return Size * Count;
}
} // namespace

CurlTransport::CurlTransport() { curl_global_init(CURL_GLOBAL_DEFAULT); }
CurlTransport::~CurlTransport() { curl_global_cleanup(); }

void CurlTransport::Send(const rean::HttpRequest& Request, rean::HttpDone Done)
{
	rean::HttpResponse Response;
	CURL* Handle = curl_easy_init();
	if (!Handle)
	{
		Done(Response);
		return;
	}
	curl_slist* Headers = nullptr;
	for (const rean::HttpHeader& H : Request.headers) Headers = curl_slist_append(Headers, (H.name + ": " + H.value).c_str());
	curl_easy_setopt(Handle, CURLOPT_URL, Request.url.c_str());
	curl_easy_setopt(Handle, CURLOPT_CUSTOMREQUEST, Request.method.c_str());
	curl_easy_setopt(Handle, CURLOPT_HTTPHEADER, Headers);
	if (!Request.body.empty())
	{
		curl_easy_setopt(Handle, CURLOPT_POSTFIELDS, Request.body.c_str());
		curl_easy_setopt(Handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(Request.body.size()));
	}
	curl_easy_setopt(Handle, CURLOPT_WRITEFUNCTION, CollectBody);
	curl_easy_setopt(Handle, CURLOPT_WRITEDATA, &Response.body);
	curl_easy_setopt(Handle, CURLOPT_HEADERFUNCTION, CollectHeader);
	curl_easy_setopt(Handle, CURLOPT_HEADERDATA, &Response.headers);
	curl_easy_setopt(Handle, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(Handle, CURLOPT_NOPROXY, "127.0.0.1,localhost");
	curl_easy_setopt(Handle, CURLOPT_NOSIGNAL, 1L);
	const CURLcode Code = curl_easy_perform(Handle);
	long Status = 0;
	if (Code == CURLE_OK) curl_easy_getinfo(Handle, CURLINFO_RESPONSE_CODE, &Status);
	Response.status = static_cast<int>(Status);
	if (verbose) std::fprintf(stderr, "  [http] %s %s -> %d%s\n", Request.method.c_str(), Request.url.c_str(), Response.status, Code == CURLE_OK ? "" : " (network error)");
	curl_slist_free_all(Headers);
	curl_easy_cleanup(Handle);
	Done(Response);
}

} // namespace reantools
