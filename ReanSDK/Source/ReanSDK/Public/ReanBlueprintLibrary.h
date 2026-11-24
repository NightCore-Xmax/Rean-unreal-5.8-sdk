// Copyright (c) REAN Studios. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReanTypes.h"
#include "ReanBlueprintLibrary.generated.h"

class UReanSubsystem;
class UTexture2D;

UCLASS()
class REANSDK_API UReanBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The REAN subsystem of this game instance (None on dedicated servers). */
	UFUNCTION(BlueprintPure, Category = "REAN", meta = (WorldContext = "WorldContextObject", DisplayName = "Get REAN Subsystem"))
	static UReanSubsystem* GetReanSubsystem(const UObject* WorldContextObject);

	/** A QR code of Text (e.g. the device code's VerificationUriComplete) as a texture with crisp pixels. */
	UFUNCTION(BlueprintCallable, Category = "REAN", meta = (AdvancedDisplay = "PixelsPerModule"))
	static UTexture2D* MakeQrCodeTexture(const FString& Text, int32 PixelsPerModule = 8);

	UFUNCTION(BlueprintPure, Category = "REAN", meta = (DisplayName = "Is OK (REAN Error)", CompactNodeTitle = "OK?"))
	static bool IsOk(const FReanError& Error) { return Error.IsOk(); }

	/** "Online", "In game", ... for display. */
	UFUNCTION(BlueprintPure, Category = "REAN")
	static FText PresenceStatusText(EReanPresenceStatus Status);

	/** A short line for a friends list, e.g. "In game: Your Game — Ranked (Round 3)". */
	UFUNCTION(BlueprintPure, Category = "REAN")
	static FText DescribePresence(const FReanPresence& Presence);

	/** The current time in unix seconds, for FReanActivity.StartedAt. */
	UFUNCTION(BlueprintPure, Category = "REAN")
	static int64 UnixSecondsNow();
};
