// Copyright (c) REAN Studios. MIT License.
//
// A blocking libcurl transport for command-line tools and tests. It completes
// each request inside Send(), which the rean::IHttpTransport contract allows.
// Games use the engine's asynchronous HTTP instead (FHttpModule in Unreal).
#pragma once

#include "Rean/ReanPlatform.h"

namespace reantools
{

class CurlTransport : public rean::IHttpTransport
{
public:
	CurlTransport();
	~CurlTransport() override;
	void Send(const rean::HttpRequest& Request, rean::HttpDone Done) override;
	bool verbose = false;  // prints method, URL and status (never headers or bodies)
};

} // namespace reantools
