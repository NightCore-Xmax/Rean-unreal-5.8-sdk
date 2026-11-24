// Copyright (c) REAN Studios. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ReanSettings.generated.h"

/**
 * Project Settings > Plugins > REAN SDK. Saved in Config/DefaultGame.ini:
 *
 *   [/Script/ReanSDK.ReanSettings]
 *   ClientId=rean_xxxxxxxxxxxxxxxxxxxxxxxx
 *
 * Create your app (and get its client id) at
 * https://devellopers-reanstudios-com.vercel.app/console. The client id is
 * public, not a secret: games are public OAuth clients and have no secret.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "REAN SDK"))
class REANSDK_API UReanSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UReanSettings();

	/** Your app's client id: rean_ followed by 24 lowercase letters or digits. */
	UPROPERTY(Config, EditAnywhere, Category = "REAN")
	FString ClientId;

	/** Space-separated scopes. Keep offline_access to keep players signed in between launches. */
	UPROPERTY(Config, EditAnywhere, Category = "REAN")
	FString Scopes = TEXT("openid profile offline_access friends.read presence.read presence.write invites");

	/** Keep the player signed in between launches (encrypted with DPAPI on Windows; see README for consoles). */
	UPROPERTY(Config, EditAnywhere, Category = "REAN")
	bool bSaveSession = true;

	/** Seconds between checks for invites while something listens (OnInviteReceived / OnInviteAnswered / OnEvent). 10 to 300. */
	UPROPERTY(Config, EditAnywhere, Category = "REAN", meta = (ClampMin = "10", ClampMax = "300", UIMin = "10", UIMax = "60"))
	int32 EventPollSeconds = 10;

	/** REAN API. https://rean-client-vyli.vercel.app is the same API, for networks that block the custom domain. */
	UPROPERTY(Config, EditAnywhere, Category = "REAN", AdvancedDisplay)
	FString ApiBase = TEXT("https://api.reanstudios.com");

	/** Browser sign-in page. */
	UPROPERTY(Config, EditAnywhere, Category = "REAN", AdvancedDisplay)
	FString AuthorizeUrl = TEXT("https://reanstudios.com/oauth/authorize");

	/** Log every SDK step to LogRean (never tokens). */
	UPROPERTY(Config, EditAnywhere, Category = "REAN", AdvancedDisplay)
	bool bVerboseLogging = false;

	/** Empty when the client id looks right, otherwise what to fix. */
	FString ValidateClientId() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
