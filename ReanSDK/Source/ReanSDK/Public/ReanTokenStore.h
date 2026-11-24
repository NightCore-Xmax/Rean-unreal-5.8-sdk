// Copyright (c) REAN Studios. MIT License.
//
// Where the REAN SDK keeps the player's refresh token (and the event cursor).
// Treat it like a password store.
//
// Defaults (IReanTokenStore::CreateDefault):
//   Windows      DPAPI (CryptProtectData), files in Saved/Rean/, readable only by the same Windows user.
//   elsewhere    a SaveGame slot, NOT encrypted (a warning is logged). Replace it on consoles and mobile.
//
// Consoles / mobile: implement IReanTokenStore with the platform's secure storage
// (console secure save data, iOS Keychain, Android Keystore-backed storage) and
// pass it to UReanSubsystem::SetTokenStore before the first sign-in.
#pragma once

#include "CoreMinimal.h"

class REANSDK_API IReanTokenStore
{
public:
	virtual ~IReanTokenStore() = default;

	/** Keys are short ASCII strings such as "rean.session.rean_…". */
	virtual bool Load(const FString& Key, FString& OutValue) = 0;
	virtual bool Save(const FString& Key, const FString& Value) = 0;
	virtual void Remove(const FString& Key) = 0;
	/** For logs, e.g. "DPAPI (Saved/Rean)". */
	virtual FString Describe() const = 0;
	/** False for stores that keep tokens in plain text (the SDK then logs a warning once). */
	virtual bool IsSecure() const = 0;

	/** DPAPI on Windows, a SaveGame slot elsewhere. */
	static TSharedRef<IReanTokenStore> CreateDefault();
	/** Unencrypted SaveGame slots ("Rean_<key>"). */
	static TSharedRef<IReanTokenStore> CreateSaveGame();
	/** In memory only: players sign in again every launch. */
	static TSharedRef<IReanTokenStore> CreateMemory();
};
