// Copyright (c) REAN Studios. MIT License.
#include "ReanSubsystem.h"

#include "ReanConvert.h"
#include "ReanHttpTransport.h"
#include "ReanLog.h"
#include "ReanLoopbackListener.h"
#include "ReanSettings.h"
#include "ReanTokenStore.h"
#include "Rean/ReanClient.h"

#include "Async/Async.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Runtime/Launch/Resources/Version.h"

#include <memory>
#include <string>

using ReanConvert::ToF;
using ReanConvert::ToStd;

namespace
{
/** ReanCore's storage interface on top of an IReanTokenStore. */
class FReanStoreAdapter final : public rean::IKeyValueStore
{
public:
	explicit FReanStoreAdapter(TSharedRef<IReanTokenStore> InStore) : Store(InStore) {}

	virtual bool Load(const std::string& Key, std::string& OutValue) override
	{
		FString Value;
		if (!Store->Load(ToF(Key), Value)) return false;
		OutValue = ToStd(Value);
		return true;
	}
	virtual bool Save(const std::string& Key, const std::string& Value) override { return Store->Save(ToF(Key), ToF(Value)); }
	virtual void Remove(const std::string& Key) override { Store->Remove(ToF(Key)); }

private:
	TSharedRef<IReanTokenStore> Store;
};

/** For calls made when there is no client (after Deinitialize): still answer once, on the game thread. */
void AnswerLater(TFunction<void()> Fn)
{
	AsyncTask(ENamedThreads::GameThread, MoveTemp(Fn));
}

FReanError NoClient()
{
	return ReanConvert::LocalError(EReanErrorCode::InvalidConfig, TEXT("The REAN subsystem is not running."));
}

rean::SignInCallback WrapSignIn(UReanSubsystem::FSignInDone Done)
{
	return [Done](const rean::Error& E, const rean::User& U) {
		if (Done) Done(ReanConvert::Error(E), ReanConvert::User(U));
	};
}
} // namespace

// ─── Lifetime ───────────────────────────────────────────────────────────────

bool UReanSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Dedicated servers have no player to sign in; they verify players another way.
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UReanSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!TokenStore.IsValid()) TokenStore = IReanTokenStore::CreateDefault();
	CreateClient();
	bInitialized = true;
}

void UReanSubsystem::Deinitialize()
{
	bInitialized = false;
	CloseLoopback();
	if (Client.IsValid()) Client->Shutdown();  // clears this game's presence (best effort)
	Client.Reset();
	Super::Deinitialize();
}

void UReanSubsystem::SetTokenStore(TSharedRef<IReanTokenStore> Store)
{
	if (Client.IsValid() && (Client->IsSignedIn() || Client->IsSigningIn()))
	{
		UE_LOG(LogRean, Warning, TEXT("SetTokenStore was called after sign-in started; the current session is dropped. Call it before signing in."));
	}
	TokenStore = Store;
	if (bInitialized) CreateClient();
}

void UReanSubsystem::CreateClient()
{
	CloseLoopback();
	bListeningForEvents = false;
	const UReanSettings* Settings = GetDefault<UReanSettings>();

	FString ClientId = Settings->ClientId.TrimStartAndEnd();
	FString ApiBase = Settings->ApiBase.TrimStartAndEnd();
#if !UE_BUILD_SHIPPING
	// For testing: -ReanClientId=rean_… -ReanApiBase=https://… on the command line.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("ReanClientId="), Override) && !Override.IsEmpty()) ClientId = Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("ReanApiBase="), Override) && !Override.IsEmpty()) ApiBase = Override;
#endif

	rean::Config Config;
	Config.clientId = ToStd(ClientId);
	Config.scopes = ToStd(Settings->Scopes);
	Config.apiBase = ToStd(ApiBase);
	Config.authorizeUrl = ToStd(Settings->AuthorizeUrl);
	Config.userAgent = std::string("rean-sdk-unreal/") + REAN_CORE_VERSION + " (UE " + std::to_string(ENGINE_MAJOR_VERSION) + "." +
	                   std::to_string(ENGINE_MINOR_VERSION) + ")";
	Config.saveSession = Settings->bSaveSession;
	Config.eventPollSeconds = Settings->EventPollSeconds;

	// A second PIE client must not share the first one's session: a refresh token works once.
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		const FWorldContext* Context = GameInstance->GetWorldContext();
		if (Context && Context->WorldType == EWorldType::PIE && Context->PIEInstance > 0)
		{
			Config.sessionKeySuffix = ".pie" + std::to_string(Context->PIEInstance);
		}
	}

	Config.transport = std::make_shared<FReanHttpTransport>();
	Config.store = std::make_shared<FReanStoreAdapter>(TokenStore.ToSharedRef());
	Config.monotonicMs = []() { return static_cast<int64_t>(FPlatformTime::Seconds() * 1000.0); };
	Config.unixMs = []() { return static_cast<int64_t>((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds()); };
	const bool bVerbose = Settings->bVerboseLogging;
	Config.log = [bVerbose](rean::LogLevel Level, const std::string& Message) {
		const FString Text = ToF(Message);
		switch (Level)
		{
		case rean::LogLevel::Error: UE_LOG(LogRean, Error, TEXT("%s"), *Text); break;
		case rean::LogLevel::Warning: UE_LOG(LogRean, Warning, TEXT("%s"), *Text); break;
		case rean::LogLevel::Info: UE_LOG(LogRean, Log, TEXT("%s"), *Text); break;
		case rean::LogLevel::Verbose:
			if (bVerbose) UE_LOG(LogRean, Log, TEXT("%s"), *Text);
			break;
		}
	};

	Client = MakeShared<rean::Client>(MoveTemp(Config));
	TWeakObjectPtr<UReanSubsystem> WeakThis(this);
	Client->SetSessionCallback([WeakThis](rean::SessionChange Change, const rean::Error& Reason) {
		if (UReanSubsystem* Self = WeakThis.Get()) Self->OnSessionChanged.Broadcast(ReanConvert::SessionChange(Change), ReanConvert::Error(Reason));
	});

	if (Client->ConfigError().Ok())
	{
		UE_LOG(LogRean, Log, TEXT("REAN SDK ready (client %s, API %s, session store: %s)."), *ClientId, *ToF(Client->GetConfig().apiBase),
			*TokenStore->Describe());
		if (Settings->bSaveSession && !TokenStore->IsSecure())
		{
			UE_LOG(LogRean, Warning, TEXT("The REAN session is saved without encryption (%s). On consoles and mobile, pass your platform's secure storage to UReanSubsystem::SetTokenStore."),
				*TokenStore->Describe());
		}
	}
}

// ─── Tick ───────────────────────────────────────────────────────────────────

void UReanSubsystem::Tick(float DeltaTime)
{
	if (!Client.IsValid()) return;
	UpdateEventListening();
	Client->Tick();
	TickLoopback();
}

TStatId UReanSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReanSubsystem, STATGROUP_Tickables);
}

bool UReanSubsystem::IsTickable() const
{
	return bInitialized && !IsTemplate();
}

void UReanSubsystem::UpdateEventListening()
{
	// Events are polled only while something listens.
	const bool bWant = OnInviteReceived.IsBound() || OnInviteAnswered.IsBound() || OnEvent.IsBound();
	if (bWant == bListeningForEvents) return;
	bListeningForEvents = bWant;
	if (bWant)
	{
		TWeakObjectPtr<UReanSubsystem> WeakThis(this);
		Client->SetEventCallback([WeakThis](const rean::Event& E) {
			if (UReanSubsystem* Self = WeakThis.Get()) Self->HandleEvent(E);
		});
	}
	else
	{
		Client->SetEventCallback(nullptr);
	}
}

void UReanSubsystem::HandleEvent(const rean::Event& E)
{
	const FReanEvent Event = ReanConvert::Event(E);
	OnEvent.Broadcast(Event);
	switch (Event.Type)
	{
	case EReanEventType::InviteReceived: OnInviteReceived.Broadcast(Event.Invite); break;
	case EReanEventType::InviteAccepted:
	case EReanEventType::InviteDeclined: OnInviteAnswered.Broadcast(Event.Answer); break;
	case EReanEventType::Unknown: break;
	}
}

// ─── Sign in ────────────────────────────────────────────────────────────────

FString UReanSubsystem::LauncherCodeFromCommandLine() const
{
	// -ReanAuthCode=rlc_… (not -LauncherToken, which is a different, server-side launch ticket).
	FString Code;
	if (FParse::Value(FCommandLine::Get(), TEXT("ReanAuthCode="), Code)) return Code.TrimStartAndEnd();
	return FString();
}

bool UReanSubsystem::WasStartedByLauncher() const
{
	return !bLauncherCodeUsed && !LauncherCodeFromCommandLine().IsEmpty();
}

void UReanSubsystem::SignInWithLauncher(FSignInDone Done)
{
	const FString Code = LauncherCodeFromCommandLine();
	if (Code.IsEmpty())
	{
		AnswerLater([Done]() {
			if (Done) Done(ReanConvert::LocalError(EReanErrorCode::InvalidArgument, TEXT("The game was not started by the REAN Launcher (no -ReanAuthCode).")), FReanUser());
		});
		return;
	}
	SignInWithLauncherCode(Code, MoveTemp(Done));
}

void UReanSubsystem::SignInWithLauncherCode(const FString& LauncherCode, FSignInDone Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanUser()); });
	bLauncherCodeUsed = true;
	Client->SignInWithLauncher(ToStd(LauncherCode), WrapSignIn(MoveTemp(Done)));
}

void UReanSubsystem::SignInWithDevice(TFunction<void(const FReanDeviceCode&)> OnCode, FSignInDone Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanUser()); });
	Client->SignInWithDevice(
		[OnCode](const rean::DeviceCode& Code) {
			if (OnCode) OnCode(ReanConvert::DeviceCode(Code));
		},
		WrapSignIn(MoveTemp(Done)));
}

void UReanSubsystem::RestoreSession(FSignInDone Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanUser()); });
	Client->RestoreSession(WrapSignIn(MoveTemp(Done)));
}

void UReanSubsystem::SignInWithRefreshToken(const FString& RefreshToken, FSignInDone Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanUser()); });
	Client->SignInWithRefreshToken(ToStd(RefreshToken), WrapSignIn(MoveTemp(Done)));
}

bool UReanSubsystem::SignInWithBrowser(FSignInDone Done, FReanError& OutError)
{
#if REAN_WITH_LOOPBACK
	if (!Client.IsValid())
	{
		OutError = NoClient();
		return false;
	}
	TSharedPtr<FReanLoopbackListener> Listener = MakeShared<FReanLoopbackListener>();
	int32 Port = 0;
	if (!Listener->Open(Port))
	{
		OutError = ReanConvert::LocalError(EReanErrorCode::NotSupported, TEXT("Could not open the local sign-in listener. Use device sign-in."));
		return false;
	}
	// RFC 8252: the app registers http://127.0.0.1/callback; REAN accepts any port.
	const std::string Redirect = "http://127.0.0.1:" + std::to_string(Port) + "/callback";
	std::string Url;
	TWeakObjectPtr<UReanSubsystem> WeakThis(this);
	const rean::Error E = Client->BeginBrowserSignIn(Redirect, Url, [WeakThis, Done](const rean::Error& Err, const rean::User& U) {
		if (UReanSubsystem* Self = WeakThis.Get()) Self->CloseLoopback();
		if (Done) Done(ReanConvert::Error(Err), ReanConvert::User(U));
	});
	if (!E.Ok())
	{
		OutError = ReanConvert::Error(E);
		return false;
	}
	Loopback = Listener;
	FString LaunchError;
	FPlatformProcess::LaunchURL(*ToF(Url), nullptr, &LaunchError);
	if (!LaunchError.IsEmpty())
	{
		UE_LOG(LogRean, Warning, TEXT("Could not open the browser (%s). Open this address to sign in: %s"), *LaunchError, *ToF(Url));
	}
	OutError = FReanError();
	return true;
#else
	OutError = ReanConvert::LocalError(EReanErrorCode::NotSupported, TEXT("Browser sign-in is only available on Windows, macOS and Linux. Use device sign-in."));
	return false;
#endif
}

void UReanSubsystem::TickLoopback()
{
	if (!Loopback.IsValid()) return;
	if (!Client->IsSigningIn())
	{
		CloseLoopback();  // cancelled or timed out
		return;
	}
	FString Target;
	if (!Loopback->Poll(Target)) return;
	if (!Target.StartsWith(TEXT("/callback")))
	{
		Loopback->Respond("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");  // e.g. /favicon.ico
		return;
	}
	const rean::Error E = Client->CompleteBrowserSignIn(ToStd(Target));
	const bool bDeclined = Target.Contains(TEXT("?error=")) || Target.Contains(TEXT("&error="));
	if (Loopback.IsValid()) Loopback->Respond(rean::LoopbackResponse(E.Ok() && !bDeclined));
}

void UReanSubsystem::CloseLoopback()
{
	if (!Loopback.IsValid()) return;
	Loopback->Close();
	Loopback.Reset();
}

void UReanSubsystem::CancelSignIn()
{
	if (Client.IsValid()) Client->CancelSignIn();
	CloseLoopback();
}

void UReanSubsystem::SignOut()
{
	CloseLoopback();
	if (Client.IsValid()) Client->SignOut();
}

bool UReanSubsystem::IsSignedIn() const
{
	return Client.IsValid() && Client->IsSignedIn();
}

bool UReanSubsystem::IsSigningIn() const
{
	return Client.IsValid() && Client->IsSigningIn();
}

FReanUser UReanSubsystem::GetCurrentUser() const
{
	return Client.IsValid() ? ReanConvert::User(Client->CurrentUser()) : FReanUser();
}

bool UReanSubsystem::HasScope(const FString& Scope) const
{
	return Client.IsValid() && Client->HasScope(ToStd(Scope.TrimStartAndEnd()));
}

// ─── Social API ─────────────────────────────────────────────────────────────

void UReanSubsystem::GetMe(TFunction<void(const FReanError&, const FReanUser&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanUser()); });
	Client->GetMe([Done](const rean::Error& E, const rean::User& U) {
		if (Done) Done(ReanConvert::Error(E), ReanConvert::User(U));
	});
}

void UReanSubsystem::GetFriends(TFunction<void(const FReanError&, const TArray<FReanFriend>&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), TArray<FReanFriend>()); });
	Client->GetFriends([Done](const rean::Error& E, const std::vector<rean::Friend>& List) {
		TArray<FReanFriend> Out;
		Out.Reserve(static_cast<int32>(List.size()));
		for (const rean::Friend& F : List) Out.Add(ReanConvert::Friend(F));
		if (Done) Done(ReanConvert::Error(E), Out);
	});
}

void UReanSubsystem::GetPresence(const TArray<FString>& PlayerIds, TFunction<void(const FReanError&, const TArray<FReanPlayerPresence>&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), TArray<FReanPlayerPresence>()); });
	std::vector<std::string> Ids;
	Ids.reserve(static_cast<size_t>(PlayerIds.Num()));
	for (const FString& Id : PlayerIds) Ids.push_back(ToStd(Id.TrimStartAndEnd()));
	Client->GetPresence(Ids, [Done](const rean::Error& E, const std::vector<rean::PlayerPresence>& List) {
		TArray<FReanPlayerPresence> Out;
		Out.Reserve(static_cast<int32>(List.size()));
		for (const rean::PlayerPresence& P : List)
		{
			FReanPlayerPresence Item;
			Item.PlayerId = ToF(P.playerId);
			Item.Presence = ReanConvert::Presence(P.presence);
			Out.Add(Item);
		}
		if (Done) Done(ReanConvert::Error(E), Out);
	});
}

void UReanSubsystem::GetInvites(TFunction<void(const FReanError&, const TArray<FReanInvite>&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), TArray<FReanInvite>()); });
	Client->GetInvites([Done](const rean::Error& E, const std::vector<rean::Invite>& List) {
		TArray<FReanInvite> Out;
		Out.Reserve(static_cast<int32>(List.size()));
		for (const rean::Invite& I : List) Out.Add(ReanConvert::Invite(I));
		if (Done) Done(ReanConvert::Error(E), Out);
	});
}

void UReanSubsystem::SendInvite(const FString& FriendId, const FString& JoinSecret, const FString& Message,
	TFunction<void(const FReanError&, const FReanSentInvite&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FReanSentInvite()); });
	Client->SendInvite(ToStd(FriendId.TrimStartAndEnd()), ToStd(JoinSecret), ToStd(Message), [Done](const rean::Error& E, const rean::SentInvite& S) {
		FReanSentInvite Out;
		Out.Id = ToF(S.id);
		Out.To = ToF(S.to);
		Out.ExpiresAt = ToF(S.expiresAt);
		if (Done) Done(ReanConvert::Error(E), Out);
	});
}

void UReanSubsystem::RespondToInvite(const FString& InviteId, bool bAccept, TFunction<void(const FReanError&, const FString&)> Done)
{
	if (!Client.IsValid()) return AnswerLater([Done]() { if (Done) Done(NoClient(), FString()); });
	Client->RespondToInvite(ToStd(InviteId.TrimStartAndEnd()), bAccept, [Done](const rean::Error& E, const std::string& Secret) {
		if (Done) Done(ReanConvert::Error(E), ToF(Secret));
	});
}

bool UReanSubsystem::SetActivity(const FReanActivity& Activity, FReanError& Error)
{
	if (!Client.IsValid())
	{
		Error = NoClient();
		return false;
	}
	const rean::Error E = Client->SetActivity(ReanConvert::Activity(Activity));
	Error = ReanConvert::Error(E);
	return E.Ok();
}

void UReanSubsystem::ClearActivity()
{
	if (Client.IsValid()) Client->ClearActivity();
}

bool UReanSubsystem::HasActivity() const
{
	return Client.IsValid() && Client->HasActivity();
}
