// Copyright (c) REAN Studios. MIT License.
#include "ReanHttpTransport.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

void FReanHttpTransport::Send(const rean::HttpRequest& Request, rean::HttpDone Done)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(UTF8_TO_TCHAR(Request.url.c_str()));
	Http->SetVerb(UTF8_TO_TCHAR(Request.method.c_str()));
	for (const rean::HttpHeader& Header : Request.headers)
	{
		Http->SetHeader(UTF8_TO_TCHAR(Header.name.c_str()), UTF8_TO_TCHAR(Header.value.c_str()));
	}
	if (!Request.body.empty())
	{
		// Exact UTF-8 bytes (SetContentAsString would re-encode).
		TArray<uint8> Body;
		Body.Append(reinterpret_cast<const uint8*>(Request.body.data()), static_cast<int32>(Request.body.size()));
		Http->SetContent(Body);
	}
	Http->SetTimeout(30.0f);
	Http->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully) {
		rean::HttpResponse Out;
		if (bConnectedSuccessfully && Response.IsValid())
		{
			Out.status = Response->GetResponseCode();
			const TArray<uint8>& Content = Response->GetContent();
			if (Content.Num() > 0) Out.body.assign(reinterpret_cast<const char*>(Content.GetData()), static_cast<size_t>(Content.Num()));
			static const TCHAR* const Wanted[] = { TEXT("Date"), TEXT("Retry-After") };
			for (const TCHAR* Name : Wanted)
			{
				const FString Value = Response->GetHeader(Name);
				if (!Value.IsEmpty()) Out.headers.push_back({ std::string(TCHAR_TO_UTF8(Name)), std::string(TCHAR_TO_UTF8(*Value)) });
			}
		}
		Done(MoveTemp(Out));  // queued by ReanCore; handled in UReanSubsystem::Tick
	});
	Http->ProcessRequest();
}
