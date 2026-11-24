// Copyright (c) REAN Studios. MIT License.
#include "ReanEditorSignInTest.h"

#include "ReanHttpTransport.h"
#include "ReanSettings.h"
#include "Rean/ReanClient.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Widgets/Notifications/SNotificationList.h"

#include <memory>
#include <string>

#define LOCTEXT_NAMESPACE "ReanSDKEditor"

DEFINE_LOG_CATEGORY_STATIC(LogReanEditor, Log, All);

namespace
{
FString ToF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
} // namespace

FReanEditorSignInTest::~FReanEditorSignInTest()
{
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
}

void FReanEditorSignInTest::Start()
{
	if (Client.IsValid()) return;
	const UReanSettings* Settings = GetDefault<UReanSettings>();
	const FString Problem = Settings->ValidateClientId();
	if (!Problem.IsEmpty())
	{
		SetNotification(FText::Format(LOCTEXT("BadClientId", "REAN test sign-in: {0}"), FText::FromString(Problem)), false, false, FString());
		UE_LOG(LogReanEditor, Warning, TEXT("REAN test sign-in: %s"), *Problem);
		return;
	}

	rean::Config Config;
	Config.clientId = ToStd(Settings->ClientId.TrimStartAndEnd());
	Config.scopes = ToStd(Settings->Scopes);
	Config.apiBase = ToStd(Settings->ApiBase);
	Config.userAgent = "rean-sdk-unreal-editor/" REAN_CORE_VERSION;
	Config.saveSession = false;
	Config.store = std::make_shared<rean::MemoryStore>();
	Config.transport = std::make_shared<FReanHttpTransport>();
	Config.monotonicMs = []() { return static_cast<int64_t>(FPlatformTime::Seconds() * 1000.0); };
	Config.unixMs = []() { return static_cast<int64_t>((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds()); };
	Config.log = [](rean::LogLevel Level, const std::string& Message) {
		if (Level >= rean::LogLevel::Warning) UE_LOG(LogReanEditor, Warning, TEXT("%s"), *ToF(Message));
		else UE_LOG(LogReanEditor, Log, TEXT("%s"), *ToF(Message));
	};
	Client = MakeShared<rean::Client>(MoveTemp(Config));
	if (!Client->ConfigError().Ok())
	{
		SetNotification(FText::FromString(ToF(Client->ConfigError().message)), false, false, FString());
		Client.Reset();
		return;
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FReanEditorSignInTest::Tick), 0.05f);
	SetNotification(LOCTEXT("Starting", "REAN: asking for a sign-in code..."), true, false, FString());

	TWeakPtr<FReanEditorSignInTest> WeakThis = AsShared();
	Client->SignInWithDevice(
		[WeakThis](const rean::DeviceCode& Code) {
			TSharedPtr<FReanEditorSignInTest> Self = WeakThis.Pin();
			if (!Self) return;
			const FString Uri = ToF(Code.verificationUri);
			const FString UserCode = ToF(Code.userCode);
			UE_LOG(LogReanEditor, Display, TEXT("REAN test sign-in: go to %s and enter %s"), *Uri, *UserCode);
			Self->SetNotification(FText::Format(LOCTEXT("ShowCode", "REAN test sign-in\nGo to {0} and enter {1}"), FText::FromString(Uri),
				FText::FromString(UserCode)), true, false, ToF(Code.verificationUriComplete));
		},
		[WeakThis](const rean::Error& Error, const rean::User& User) {
			TSharedPtr<FReanEditorSignInTest> Self = WeakThis.Pin();
			if (!Self) return;
			if (!Error.Ok())
			{
				Self->Finish(false, FString::Printf(TEXT("%s (%s)"), *ToF(Error.message), UTF8_TO_TCHAR(rean::ErrorCodeName(Error.code))));
				return;
			}
			UE_LOG(LogReanEditor, Display, TEXT("REAN test sign-in OK: %s (@%s), player id %s, scopes: %s"), *ToF(User.displayName), *ToF(User.username),
				*ToF(User.id), *ToF(Self->Client->GrantedScopes()));
			// One Social API call to prove the token works, then revoke the test session.
			Self->Client->GetFriends([WeakThis, User](const rean::Error& FriendsError, const std::vector<rean::Friend>& Friends) {
				TSharedPtr<FReanEditorSignInTest> Inner = WeakThis.Pin();
				if (!Inner) return;
				const FString FriendsLine = FriendsError.Ok() ? FString::Printf(TEXT("%d friend(s)"), static_cast<int32>(Friends.size()))
				                                              : FString::Printf(TEXT("friends: %s"), *ToF(FriendsError.message));
				Inner->Finish(true, FString::Printf(TEXT("Signed in as %s (@%s). %s."), *ToF(User.displayName), *ToF(User.username), *FriendsLine));
			});
		});
}

void FReanEditorSignInTest::Cancel()
{
	if (Client.IsValid()) Client->CancelSignIn();
}

bool FReanEditorSignInTest::Tick(float DeltaTime)
{
	if (!Client.IsValid()) return false;
	Client->Tick();
	if (StopAt > 0.0 && FPlatformTime::Seconds() >= StopAt)
	{
		Client.Reset();
		TickerHandle.Reset();
		return false;  // removes the ticker
	}
	return true;
}

void FReanEditorSignInTest::Finish(bool bSuccess, const FString& Message)
{
	UE_LOG(LogReanEditor, Display, TEXT("REAN test sign-in %s: %s"), bSuccess ? TEXT("succeeded") : TEXT("failed"), *Message);
	SetNotification(FText::FromString(Message), false, bSuccess, FString());
	if (Client.IsValid())
	{
		Client->SignOut();  // revoke the test session
		StopAt = FPlatformTime::Seconds() + 3.0;  // let the revoke go out
	}
}

void FReanEditorSignInTest::SetNotification(const FText& Text, bool bPending, bool bSuccess, const FString& OpenUrl)
{
	TSharedPtr<SNotificationItem> Item = Notification.Pin();
	if (Item.IsValid() && (!bPending || !OpenUrl.IsEmpty()))
	{
		Item->ExpireAndFadeout();
		Item.Reset();
	}
	if (!Item.IsValid())
	{
		FNotificationInfo Info(Text);
		Info.bFireAndForget = !bPending;
		Info.ExpireDuration = bPending ? 600.0f : 8.0f;
		Info.FadeOutDuration = 0.5f;
		Info.bUseLargeFont = false;
		Info.WidthOverride = 420.0f;
		if (!OpenUrl.IsEmpty())
		{
			Info.Hyperlink = FSimpleDelegate::CreateLambda([OpenUrl]() { FPlatformProcess::LaunchURL(*OpenUrl, nullptr, nullptr); });
			Info.HyperlinkText = LOCTEXT("OpenActivate", "Open the activation page");
		}
		if (bPending)
		{
			TWeakPtr<FReanEditorSignInTest> WeakThis = AsShared();
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("CancelButton", "Cancel"), LOCTEXT("CancelTip", "Cancel the test sign-in"),
				FSimpleDelegate::CreateLambda([WeakThis]() {
					if (TSharedPtr<FReanEditorSignInTest> Self = WeakThis.Pin()) Self->Cancel();
				}),
				SNotificationItem::CS_Pending));
		}
		Item = FSlateNotificationManager::Get().AddNotification(Info);
		Notification = Item;
	}
	else
	{
		Item->SetText(Text);
	}
	if (Item.IsValid()) Item->SetCompletionState(bPending ? SNotificationItem::CS_Pending : (bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail));
}

#undef LOCTEXT_NAMESPACE
