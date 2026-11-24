// Copyright (c) REAN Studios. MIT License.
//
// ReanCore's HTTP transport on Unreal's HTTP module (FHttpModule). Answers are
// handed to ReanCore, which delivers them on the next UReanSubsystem tick.
#pragma once

#include "CoreMinimal.h"
#include "Rean/ReanPlatform.h"

class REANSDK_API FReanHttpTransport : public rean::IHttpTransport
{
public:
	virtual void Send(const rean::HttpRequest& Request, rean::HttpDone Done) override;
};
