// Copyright (c) REAN Studios. MIT License.
#include "ReanBlueprintLibrary.h"

#include "ReanConvert.h"
#include "ReanSubsystem.h"
#include "Rean/ReanQrCode.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TextureResource.h"

#define LOCTEXT_NAMESPACE "ReanSDK"

UReanSubsystem* UReanBlueprintLibrary::GetReanSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UReanSubsystem>() : nullptr;
}

UTexture2D* UReanBlueprintLibrary::MakeQrCodeTexture(const FString& Text, int32 PixelsPerModule)
{
	rean::QrCode Qr;
	if (Text.IsEmpty() || !rean::QrCode::Encode(ReanConvert::ToStd(Text), rean::QrCode::Ecc::Medium, Qr)) return nullptr;
	const int32 Scale = FMath::Clamp(PixelsPerModule, 1, 32);
	constexpr int32 Border = 4;  // the quiet zone scanners need
	const int32 Side = (Qr.Size() + Border * 2) * Scale;
	UTexture2D* Texture = UTexture2D::CreateTransient(Side, Side, PF_B8G8R8A8);
	if (!Texture) return nullptr;
	Texture->Filter = TF_Nearest;  // crisp modules when scaled
	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0) return nullptr;
	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Pixels) return nullptr;
	for (int32 Y = 0; Y < Side; Y++)
	{
		for (int32 X = 0; X < Side; X++)
		{
			const bool bDark = Qr.Dark(X / Scale - Border, Y / Scale - Border);
			Pixels[Y * Side + X] = bDark ? FColor::Black : FColor::White;
		}
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

FText UReanBlueprintLibrary::PresenceStatusText(EReanPresenceStatus Status)
{
	switch (Status)
	{
	case EReanPresenceStatus::Online: return LOCTEXT("Online", "Online");
	case EReanPresenceStatus::Away: return LOCTEXT("Away", "Away");
	case EReanPresenceStatus::DoNotDisturb: return LOCTEXT("DoNotDisturb", "Do not disturb");
	case EReanPresenceStatus::InGame: return LOCTEXT("InGame", "In game");
	case EReanPresenceStatus::Offline: return LOCTEXT("Offline", "Offline");
	case EReanPresenceStatus::Unknown: break;
	}
	return LOCTEXT("Unknown", "Unknown");
}

FText UReanBlueprintLibrary::DescribePresence(const FReanPresence& Presence)
{
	const FText Status = PresenceStatusText(Presence.Status);
	if (!Presence.bHasActivity || Presence.Activity.AppName.IsEmpty()) return Status;
	FString Line = Presence.Activity.AppName;
	if (!Presence.Activity.Details.IsEmpty()) Line += TEXT(" - ") + Presence.Activity.Details;
	if (!Presence.Activity.State.IsEmpty()) Line += TEXT(" (") + Presence.Activity.State + TEXT(")");
	if (Presence.Activity.bHasParty && Presence.Activity.PartyMax > 0)
	{
		Line += FString::Printf(TEXT(" [%d/%d]"), FMath::Max(0, Presence.Activity.PartySize), Presence.Activity.PartyMax);
	}
	return FText::Format(LOCTEXT("PresenceLine", "{0}: {1}"), Status, FText::FromString(Line));
}

int64 UReanBlueprintLibrary::UnixSecondsNow()
{
	return FDateTime::UtcNow().ToUnixTimestamp();
}

#undef LOCTEXT_NAMESPACE
