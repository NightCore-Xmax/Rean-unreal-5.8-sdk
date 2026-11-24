// Copyright (c) REAN Studios. MIT License.
//
// Token stores. This is the only file with platform-specific calls (DPAPI on
// Windows); everything else in the plugin is portable.
#include "ReanTokenStore.h"

#include "ReanLog.h"
#include "ReanSecretSave.h"

#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
/** Keys become file / slot names: keep [A-Za-z0-9._-]. */
FString SafeName(const FString& Key)
{
	FString Out;
	Out.Reserve(Key.Len());
	for (const TCHAR C : Key)
	{
		const bool bOk = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('0') && C <= TEXT('9')) ||
		                 C == TEXT('.') || C == TEXT('-') || C == TEXT('_');
		Out.AppendChar(bOk ? C : TEXT('_'));
	}
	return Out;
}

class FReanMemoryStore final : public IReanTokenStore
{
public:
	virtual bool Load(const FString& Key, FString& OutValue) override
	{
		const FString* Found = Values.Find(Key);
		if (!Found) return false;
		OutValue = *Found;
		return true;
	}
	virtual bool Save(const FString& Key, const FString& Value) override
	{
		Values.Add(Key, Value);
		return true;
	}
	virtual void Remove(const FString& Key) override { Values.Remove(Key); }
	virtual FString Describe() const override { return TEXT("memory (not saved)"); }
	virtual bool IsSecure() const override { return true; }

private:
	TMap<FString, FString> Values;
};

class FReanSaveGameStore final : public IReanTokenStore
{
public:
	virtual bool Load(const FString& Key, FString& OutValue) override
	{
		const FString Slot = SlotName(Key);
		if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) return false;
		const UReanSecretSave* Save = Cast<UReanSecretSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
		if (!Save) return false;
		OutValue = Save->Value;
		return true;
	}
	virtual bool Save(const FString& Key, const FString& Value) override
	{
		UReanSecretSave* Save = Cast<UReanSecretSave>(UGameplayStatics::CreateSaveGameObject(UReanSecretSave::StaticClass()));
		if (!Save) return false;
		Save->Value = Value;
		return UGameplayStatics::SaveGameToSlot(Save, SlotName(Key), 0);
	}
	virtual void Remove(const FString& Key) override
	{
		const FString Slot = SlotName(Key);
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0)) UGameplayStatics::DeleteGameInSlot(Slot, 0);
	}
	virtual FString Describe() const override { return TEXT("SaveGame slots Rean_* (not encrypted)"); }
	virtual bool IsSecure() const override { return false; }

private:
	static FString SlotName(const FString& Key) { return TEXT("Rean_") + SafeName(Key); }
};

} // namespace

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
namespace
{
/**
 * DPAPI: CryptProtectData ties the data to the Windows user account, so the
 * files in Saved/Rean are useless when copied to another account or PC.
 */
class FReanDpapiStore final : public IReanTokenStore
{
public:
	FReanDpapiStore() : Folder(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Rean"))) {}

	virtual bool Load(const FString& Key, FString& OutValue) override
	{
		TArray<uint8> Sealed;
		if (!FFileHelper::LoadFileToArray(Sealed, *PathOf(Key), FILEREAD_Silent) || Sealed.Num() == 0) return false;
		DATA_BLOB In;
		In.pbData = Sealed.GetData();
		In.cbData = static_cast<DWORD>(Sealed.Num());
		DATA_BLOB Entropy = MakeEntropy();
		DATA_BLOB Out = {};
		if (!CryptUnprotectData(&In, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			UE_LOG(LogRean, Warning, TEXT("Could not decrypt the saved REAN session (another Windows user or PC?). The player has to sign in again."));
			return false;
		}
		FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Out.pbData), static_cast<int32>(Out.cbData));
		OutValue = FString(Converted.Length(), Converted.Get());
		SecureZero(Out.pbData, Out.cbData);
		LocalFree(Out.pbData);
		return true;
	}

	virtual bool Save(const FString& Key, const FString& Value) override
	{
		FTCHARToUTF8 Utf8(*Value);
		DATA_BLOB In;
		In.pbData = reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(reinterpret_cast<const ANSICHAR*>(Utf8.Get())));
		In.cbData = static_cast<DWORD>(Utf8.Length());
		DATA_BLOB Entropy = MakeEntropy();
		DATA_BLOB Out = {};
		if (!CryptProtectData(&In, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out)) return false;
		TArray<uint8> Sealed;
		Sealed.Append(Out.pbData, static_cast<int32>(Out.cbData));
		LocalFree(Out.pbData);
		// Write a temp file, then move it over: a crash mid-write never leaves half a token.
		const FString Final = PathOf(Key);
		const FString Temp = Final + TEXT(".tmp");
		IFileManager::Get().MakeDirectory(*Folder, true);
		if (!FFileHelper::SaveArrayToFile(Sealed, *Temp)) return false;
		return IFileManager::Get().Move(*Final, *Temp, /*bReplace*/ true);
	}

	virtual void Remove(const FString& Key) override { IFileManager::Get().Delete(*PathOf(Key), false, true, true); }
	virtual FString Describe() const override { return TEXT("DPAPI (Saved/Rean)"); }
	virtual bool IsSecure() const override { return true; }

private:
	FString Folder;

	FString PathOf(const FString& Key) const { return FPaths::Combine(Folder, SafeName(Key) + TEXT(".bin")); }

	static DATA_BLOB MakeEntropy()
	{
		static const char Text[] = "REAN SDK session v1";
		DATA_BLOB B;
		B.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(Text));
		B.cbData = static_cast<DWORD>(sizeof(Text) - 1);
		return B;
	}

	static void SecureZero(BYTE* Data, DWORD Size)
	{
		volatile BYTE* P = Data;
		for (DWORD I = 0; I < Size; I++) P[I] = 0;
	}
};

} // namespace
#include "Windows/HideWindowsPlatformTypes.h"
#endif

TSharedRef<IReanTokenStore> IReanTokenStore::CreateDefault()
{
#if PLATFORM_WINDOWS
	return MakeShared<FReanDpapiStore>();
#else
	return CreateSaveGame();
#endif
}

TSharedRef<IReanTokenStore> IReanTokenStore::CreateSaveGame()
{
	return MakeShared<FReanSaveGameStore>();
}

TSharedRef<IReanTokenStore> IReanTokenStore::CreateMemory()
{
	return MakeShared<FReanMemoryStore>();
}
