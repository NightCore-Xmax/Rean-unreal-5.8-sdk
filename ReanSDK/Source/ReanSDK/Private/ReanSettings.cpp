// Copyright (c) REAN Studios. MIT License.
#include "ReanSettings.h"

#include "ReanLog.h"
#include "Rean/ReanClient.h"

#define LOCTEXT_NAMESPACE "ReanSettings"

UReanSettings::UReanSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("REAN SDK");
}

FString UReanSettings::ValidateClientId() const
{
	const FString Id = ClientId.TrimStartAndEnd();
	if (Id.IsEmpty())
	{
		return TEXT("Set your client id. Create an app at https://devellopers-reanstudios-com.vercel.app/console and copy its client id.");
	}
	if (!rean::IsValidClientId(std::string(TCHAR_TO_UTF8(*Id))))
	{
		return TEXT("A client id is rean_ followed by 24 lowercase letters or digits (29 characters in all).");
	}
	if (Id == TEXT("rean_xxxxxxxxxxxxxxxxxxxxxxxx"))
	{
		return TEXT("This is the placeholder from the documentation. Use your app's own client id.");
	}
	return FString();
}

#if WITH_EDITOR
void UReanSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UReanSettings, ClientId))
	{
		ClientId = ClientId.TrimStartAndEnd();
		const FString Problem = ValidateClientId();
		if (!Problem.IsEmpty()) UE_LOG(LogRean, Warning, TEXT("REAN SDK: %s"), *Problem);
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UReanSettings, Scopes))
	{
		if (!Scopes.Contains(TEXT("offline_access")) && bSaveSession)
		{
			UE_LOG(LogRean, Warning, TEXT("REAN SDK: without offline_access REAN gives no refresh token, so players sign in every launch."));
		}
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UReanSettings, ApiBase))
	{
		ApiBase = ApiBase.TrimStartAndEnd();
		if (!ApiBase.StartsWith(TEXT("https://"))) UE_LOG(LogRean, Warning, TEXT("REAN SDK: ApiBase must be an https:// address."));
	}
}

FText UReanSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "REAN SDK");
}

FText UReanSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Sign in with REAN and the REAN Social API (friends, presence, invites).");
}
#endif

#undef LOCTEXT_NAMESPACE
