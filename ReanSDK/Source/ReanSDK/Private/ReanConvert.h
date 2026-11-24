// Copyright (c) REAN Studios. MIT License.
//
// Conversions between ReanCore (UTF-8 std::string, rean:: structs) and the
// Unreal types of the plugin (FString, USTRUCTs).
#pragma once

#include "CoreMinimal.h"
#include "ReanTypes.h"
#include "Rean/ReanTypes.h"

#include <string>
#include <vector>

namespace ReanConvert
{
inline FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
inline std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

REANSDK_API FReanError Error(const rean::Error& E);
REANSDK_API FReanError LocalError(EReanErrorCode Code, const FString& Message);
REANSDK_API FReanUser User(const rean::User& U);
REANSDK_API FReanPresence Presence(const rean::Presence& P);
REANSDK_API FReanFriend Friend(const rean::Friend& F);
REANSDK_API FReanInvite Invite(const rean::Invite& I);
REANSDK_API FReanEvent Event(const rean::Event& E);
REANSDK_API FReanDeviceCode DeviceCode(const rean::DeviceCode& D);
REANSDK_API rean::Activity Activity(const FReanActivity& A);
REANSDK_API EReanSessionChange SessionChange(rean::SessionChange C);
} // namespace ReanConvert
