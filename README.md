# REAN SDK for Unreal Engine 5.8

Sign players in with **REAN ID** and use the **REAN Social API**: friends, presence, rich presence and game invites. Works from Blueprints and C++.

> **Preview: not yet compiled in UE 5.8.**
> The engine-independent core (`ReanCore`) is built and tested on every push on Windows (MSVC), Linux (gcc, clang, sanitizers) and macOS, and was tested end to end against the REAN API's own route handlers. The Unreal modules (`ReanSDK`, `ReanSDKEditor`) and the sample have **not** been compiled in Unreal Engine yet. If you hit a build error, please [open an issue](../../issues) with the compiler output.

Documentation: https://devellopers-reanstudios-com.vercel.app/docs/sdk/unreal

## Contents

- [What you get](#what-you-get)
- [Install](#install)
- [Set up your app](#set-up-your-app)
- [Quick start: Blueprints](#quick-start-blueprints)
- [Quick start: C++](#quick-start-c)
- [Sign-in methods](#sign-in-methods)
- [Friends, presence and invites](#friends-presence-and-invites)
- [Token storage](#token-storage)
- [Troubleshooting](#troubleshooting)
- [Privacy and player trust](#privacy-and-player-trust)
- [Repository layout and tests](#repository-layout-and-tests)

## What you get

| | |
|---|---|
| **Sign in** | Device code (consoles, TVs, any game: the player enters a code at reanstudios.com/activate, a QR code is included), REAN Launcher one-time code (`-ReanAuthCode`), saved session (rotating refresh token), browser sign-in with PKCE (desktop). |
| **Session** | Refresh a minute before the access token expires and once after a 401, never two refreshes at a time; rotating refresh tokens saved before anything else; a reused or revoked session signs the player out cleanly; late answers after Cancel / Sign Out change nothing. |
| **Social** | Friends with presence, presence lookup, rich presence kept alive every 60 s, invites with a join secret, invite events (received / accepted / declined). |
| **Blueprints** | Async nodes with *On Success* / *On Failure* pins, events, Project Settings page, a QR code texture for the device code. |
| **Editor** | Client id check and a **Test sign-in** button in Project Settings (also the console command `Rean.TestSignIn`). |
| **Storage** | Windows: DPAPI-encrypted files in `Saved/Rean`. Other platforms: a SaveGame slot with a warning; plug in console / mobile secure storage with `IReanTokenStore`. |

No third-party libraries: HTTP goes through Unreal's HTTP module, and JSON, SHA-256 (for PKCE) and the QR encoder are part of `ReanCore`.

## Install

1. Copy the `ReanSDK` folder of this repository into your project's `Plugins` folder, so you have `YourProject/Plugins/ReanSDK/ReanSDK.uplugin`. (A Fab listing will follow once the plugin has been compiled and checked.)
2. Your project needs to be a C++ project for Unreal to compile the plugin. A Blueprint-only project can add one empty C++ class (*Tools › New C++ Class*) to become one.
3. Right-click your `.uproject` › *Generate Visual Studio project files*, then build the *Development Editor* target (Visual Studio 2022 on Windows).
4. Open the editor. The plugin is under *Edit › Plugins › Online Platform › REAN SDK* and enabled by default.

Supported: Win64 first. The code has no Windows-only calls outside the token store, so Mac, Linux, Android and iOS should compile too (untested). Browser sign-in is desktop only.

## Set up your app

1. Create an app in the REAN developer console: https://devellopers-reanstudios-com.vercel.app/console
2. Copy its **client id** (`rean_` followed by 24 lowercase letters or digits). It is public, not a secret: games are *public clients* and have no client secret. Never put a client secret in a game.
3. In Unreal: *Edit › Project Settings › Plugins › REAN SDK* › paste the **Client Id**. This writes `Config/DefaultGame.ini`:

   ```ini
   [/Script/ReanSDK.ReanSettings]
   ClientId=rean_xxxxxxxxxxxxxxxxxxxxxxxx
   Scopes=openid profile offline_access friends.read presence.read presence.write invites
   bSaveSession=True
   EventPollSeconds=10
   ```
4. Press **Sign in with a device code** under *Test* on the same page. A notification shows a code; enter it at https://reanstudios.com/activate with your REAN account. The Output Log then prints your player id, username and granted scopes. Nothing is saved, and the test session is revoked at the end.

| Setting | Default | |
|---|---|---|
| Client Id | (empty) | Your app's client id. |
| Scopes | `openid profile offline_access friends.read presence.read presence.write invites` | Ask only for what you use; players see each one. Keep `offline_access` to stay signed in between launches. |
| Save Session | on | Keep the refresh token (see [Token storage](#token-storage)). |
| Event Poll Seconds | 10 | How often invites are checked while something listens (10 to 300). |
| Api Base (advanced) | `https://api.reanstudios.com` | `https://rean-client-vyli.vercel.app` is the same API, for networks that block the custom domain. |
| Authorize Url (advanced) | `https://reanstudios.com/oauth/authorize` | Browser sign-in page. |
| Verbose Logging (advanced) | off | Every SDK step in `LogRean` (tokens are never logged). |

For browser sign-in, also register the redirect URI `http://127.0.0.1/callback` for your app in the console (any port is accepted at sign-in time).

In development builds you can override the settings from the command line: `-ReanClientId=rean_… -ReanApiBase=https://…`.

## Quick start: Blueprints

All nodes are under the **REAN** category. Every async node fires exactly one of *On Success* / *On Failure*, on a later frame, with an `Error` (`Code`, `HttpStatus`, `ApiCode`, `Message`).

**Sign in** (e.g. in your main menu widget's *Event Construct*):

```
Get REAN Subsystem ─► Was Started By Launcher?
   true  ─► Sign In With Launcher ── On Success ─► (signed in)
                                  └─ On Failure ─► Restore Session (below)
   false ─► Restore Session ── On Success ─► (signed in)
                            └─ On Failure ─► Error.Code == No Saved Session ?
                                               ─► show a "Sign in with REAN" button

"Sign in with REAN" button ─► Sign In With Device
     On Code    ─► show "Go to {Code.Verification Uri} and enter {Code.User Code}"
                   and Set Brush from Texture (QR Code) on an Image
     On Success ─► (signed in: User.Display Name, User.Id)
     On Failure ─► Denied: "Sign-in was declined" · Expired: offer to start again
```

**Friends, presence, invites:**

| Node / function | |
|---|---|
| **Get Friends** | `Friends` with `User` and `Presence` (status, game, details, state, party when they are in your game). |
| **Get Presence** | Presence of up to 100 player ids. |
| **Set Activity** / **Clear Activity** | Rich presence (`Details`, `State`, `Party Id`, `Party Size`, `Party Max`, `Joinable`, `Started At` in unix seconds). Re-sent every 60 s until cleared. |
| **Send Invite** | `Friend Id`, `Join Secret` (≤ 256: your lobby id or server ticket), `Message` (≤ 140). |
| **Get Invites** | Pending invites. |
| **Respond To Invite** | Accepting returns the inviter's `Join Secret`. |
| **On Invite Received** / **On Invite Answered** / **On Event** | Bind these on the subsystem; invites are polled while one is bound. |
| **On Session Changed** | Signed in, signed out, or *Session Ended* (the player removed your app, or the session was revoked): show sign-in again. |
| **Sign Out** | Clears presence, revokes the session on REAN, forgets the saved session. |
| **Make QR Code Texture** | Any text as a crisp QR code texture. |

A complete walk-through is in [Samples/ReanSample/BLUEPRINT_SAMPLE.md](Samples/ReanSample/BLUEPRINT_SAMPLE.md).

## Quick start: C++

Add `"ReanSDK"` to your module's dependencies in its `.Build.cs`, then:

```cpp
#include "ReanSubsystem.h"

void UMyGameInstance::Init()
{
    Super::Init();
    UReanSubsystem* Rean = GetSubsystem<UReanSubsystem>();
    TWeakObjectPtr<UMyGameInstance> WeakThis(this);

    auto OnSignedIn = [WeakThis](const FReanError& Error, const FReanUser& User)
    {
        if (!Error.IsOk()) { UE_LOG(LogTemp, Warning, TEXT("REAN: %s"), *Error.Message); return; }
        UE_LOG(LogTemp, Log, TEXT("Signed in as %s (%s)"), *User.DisplayName, *User.Id);  // Id is the permanent key
    };

    if (Rean->WasStartedByLauncher())
    {
        Rean->SignInWithLauncher(OnSignedIn);
        return;
    }
    TWeakObjectPtr<UReanSubsystem> WeakRean(Rean);
    Rean->RestoreSession([WeakRean, OnSignedIn](const FReanError& Error, const FReanUser& User)
    {
        if (Error.IsOk() || !WeakRean.IsValid()) return OnSignedIn(Error, User);
        if (Error.Code == EReanErrorCode::NoSavedSession || Error.Code == EReanErrorCode::SessionEnded)
        {
            WeakRean->SignInWithDevice([](const FReanDeviceCode& Code)
            {
                // Show Code.UserCode and Code.VerificationUri; UReanBlueprintLibrary::MakeQrCodeTexture(Code.VerificationUriComplete)
            }, OnSignedIn);
        }
    });
}
```

```cpp
// Friends with presence
Rean->GetFriends([](const FReanError& Error, const TArray<FReanFriend>& Friends) { /* ... */ });

// Rich presence: kept alive every 60 s until ClearActivity() / SignOut()
FReanActivity Activity;
Activity.Details = TEXT("Ranked");
Activity.State = TEXT("Round 1");
Activity.PartyId = LobbyId;
Activity.PartySize = 1;
Activity.PartyMax = 5;
Activity.bJoinable = true;
Activity.StartedAt = FDateTime::UtcNow().ToUnixTimestamp();
FReanError Error;
Rean->SetActivity(Activity, Error);

// Invites (events are polled while a delegate is bound)
Rean->OnInviteReceived.AddDynamic(this, &UMyMenu::HandleInvite);  // UFUNCTION(const FReanInvite&)
Rean->SendInvite(FriendId, LobbyId, TEXT("Join me!"), [](const FReanError& Error, const FReanSentInvite& Invite) {});
Rean->RespondToInvite(InviteId, true, [](const FReanError& Error, const FString& JoinSecret) { /* join */ });
```

C++ rules: callbacks run later on the game thread, exactly once each; capture `TWeakObjectPtr`s, not raw `this`. The engine-independent client is available as `GetCoreClient()` (see `ReanCore`'s `Rean/ReanClient.h`).

## Sign-in methods

| Method | When | Notes |
|---|---|---|
| **Device code** (`Sign In With Device`) | Every game, console, TV, Steam Deck | Show `UserCode` + `VerificationUri` (and the QR code of `VerificationUriComplete`). The player approves on their phone or PC. The SDK polls every `interval` seconds, adds 5 s on `slow_down`, and stops on `access_denied` or `expired_token` (10 minutes). |
| **REAN Launcher** (`Sign In With Launcher`) | Games started by the REAN Launcher | The launcher passes `-ReanAuthCode=rlc_…` (single use, 5 minutes). *The launcher update that sends it is not released yet*, and REAN enables it per game; keep device sign-in as the fallback. The SDK never sends the same code twice (a second use would make REAN revoke the session). Not the same as `-LauncherToken`, which is a server-side launch ticket. |
| **Saved session** (`Restore Session`) | Every launch after the first | Needs `offline_access`. Refresh tokens last 60 days and rotate on every use. *No Saved Session* means: show sign-in. *Session Ended* means: the session was revoked or expired; show sign-in. A network error keeps the saved session for next time. |
| **Browser** (`Sign In With Browser`) | Desktop, optional | Opens reanstudios.com in the system browser; the redirect comes back to `http://127.0.0.1:<port>/callback` (a listener bound to the loopback address only). PKCE S256, `state` and `nonce` are checked. Register `http://127.0.0.1/callback` for your app. |

Only one sign-in runs at a time (*Busy*), and signing in while a player is signed in is refused; call **Sign Out** first. *Cancel Sign In* stops a running sign-in.

**Play In Editor with several players:** each extra PIE client gets its own saved session (key suffix `.pie1`, `.pie2`, …), so they can sign in with different accounts. Two copies of a packaged game on one PC share the saved session; the second one to refresh would sign both out, because a refresh token works once.

## Friends, presence and invites

- **Friends**: accepted REAN friends sorted by display name. `Presence` is filled only with `presence.read` (`bHasPresence`).
- **Presence**: `Offline`, `Online`, `Away`, `DoNotDisturb`, `InGame`. A friend in *your* game also shows `PartyId`, `PartySize`, `PartyMax` and `bJoinable` (`bInYourGame`). Non-friends always read as offline.
- **Rich presence**: REAN forgets presence 120 seconds after the last update, so the SDK re-sends it every 60 seconds while an activity is set, spaces quick updates at least 5 s apart (REAN allows 120 per player per 10 minutes), clears it on Sign Out and when the game closes, and stops (with an error in the log) if REAN refuses it, e.g. without `presence.write`.
- **Invites**: only to friends, inside your app; they expire after 10 minutes; at most 30 per player per 10 minutes. The join secret is given only to the invited friend when they accept: treat it as a short-lived ticket.
- **Events**: polled every `EventPollSeconds` (default 10) while one of the event delegates is bound. The cursor is kept per player, so events are not delivered twice, and after a restart only what is still relevant is delivered: invites that have not expired, and answers from the last 15 minutes.

## Token storage

The refresh token is a password-equivalent: anyone holding it can act as the player in your app for up to 60 days.

| Platform | Default store |
|---|---|
| Windows | DPAPI (`CryptProtectData`), files in `Saved/Rean/`. Only the same Windows user on the same PC can decrypt them. |
| Everything else | A SaveGame slot (`Rean_rean.session.<client id>`), **not encrypted**. The SDK logs a warning. |

On consoles and mobile, implement `IReanTokenStore` with the platform's secure storage and set it before the first sign-in:

```cpp
class FMyConsoleTokenStore : public IReanTokenStore
{
public:
    virtual bool Load(const FString& Key, FString& OutValue) override { /* console secure save data */ }
    virtual bool Save(const FString& Key, const FString& Value) override { /* ... */ }
    virtual void Remove(const FString& Key) override { /* ... */ }
    virtual FString Describe() const override { return TEXT("console secure save"); }
    virtual bool IsSecure() const override { return true; }
};

void UMyGameInstance::Init()
{
    Super::Init();
    GetSubsystem<UReanSubsystem>()->SetTokenStore(MakeShared<FMyConsoleTokenStore>());
}
```

Use the console vendor's secure / encrypted save-data API (see your platform's developer documentation), the iOS Keychain, or Android Keystore-backed storage. `IReanTokenStore::CreateMemory()` keeps nothing between launches. To manage the token yourself, turn *Save Session* off and use `SignInWithRefreshToken`.

## Troubleshooting

`FReanError.Code` tells you what to do; `ApiCode` and `Message` carry REAN's own words.

| Code | REAN says | What to do |
|---|---|---|
| `InvalidConfig` | | The client id is empty or malformed (`rean_` + 24 lowercase letters/digits), or Api Base is not `https://`. Fix *Project Settings › Plugins › REAN SDK*. |
| `InvalidClient` | `invalid_client` (401) | REAN does not know the client id, or the app is suspended. Check the id in the console. |
| `MissingScope` | `invalid_scope` (400), `insufficient_scope` (403) | Your app asked for a scope it may not use, or did not ask for the one this call needs (`friends.read`, `presence.read`, `presence.write`, `invites`, `profile`). Add it to *Scopes* (players sign in again to grant it). |
| `Denied` | `access_denied` | The player declined. Don't retry on your own. |
| `Expired` | `expired_token`, `expired` (410) | The device code expired (offer *Sign in* again), or the invite expired / was already answered. |
| `InvalidGrant` | `invalid_grant` | The launcher code (or browser code) is invalid, expired or already used. Fall back to the saved session or device sign-in. |
| `SessionEnded` | `invalid_grant` on refresh, `invalid_token` (401) twice | The player removed your app (*Account › Connected apps*), the session expired (60 days), or an old refresh token was reused (REAN then revokes the whole chain). The saved session is deleted; show sign-in. |
| `NoSavedSession` | | Nothing to restore: show sign-in. |
| `NotFriends` | `not_friends` (403) | Invites only go to accepted friends. |
| `NotFound` | 404 | E.g. an invite that is not this player's. |
| `RateLimited` | 429, `slow_down` | Limits: 600 requests per IP per 10 min, token 300, device codes 60, presence 120 per player, invites 30 per player. The SDK backs off its own polling; wait before retrying your own calls. |
| `Network` | | REAN could not be reached. If your network blocks `api.reanstudios.com`, set Api Base to `https://rean-client-vyli.vercel.app` (same API). |
| `Server` | 5xx, `temporarily_unavailable` | Try again later. |
| `Busy` | | A sign-in is running, or a player is signed in: Cancel / Sign Out first. |
| `NotSupported` | | E.g. browser sign-in on a console. Use device sign-in. |
| `InvalidArgument` | | A value is empty, too long (details/state/party id ≤ 128, join secret ≤ 256, message ≤ 140 characters) or malformed (player ids are UUIDs; `StartedAt` is unix **seconds**). |

Other things to check:

- **Nothing happens**: the subsystem ticks with the game, so callbacks need a running game (not just the editor). In the editor, use the **Test sign-in** button.
- **"Device sign-in works, launcher sign-in fails"**: the launcher only passes codes to games REAN has enabled for it, after the launcher update that sends them.
- **Presence not visible to friends**: the app needs `presence.write`, friends need `presence.read`, and only friends see it. Party / join information is shown only to friends in the same game.
- **Logs**: filter the Output Log by `LogRean`; turn on *Verbose Logging* for every step. Tokens are never logged.

## Privacy and player trust

- **Never ask for a REAN password** in your game. Sign-in happens only on reanstudios.com (browser), in the REAN Launcher, or with a device code.
- Use the player id (`User.Id`, the OpenID `sub`) as the key in your database. Usernames and emails can change.
- Ask only for the scopes you use. `email` is not in the default list; add it only if you need it.
- Respect players removing your app in *Connected apps*: the SDK reports `SessionEnded` and deletes the saved session; don't try to work around it.
- Never log, upload or send tokens to analytics. Keep refresh tokens in secure storage.
- Sign Out revokes the session on REAN, so offer it in your settings menu.

## Repository layout and tests

| Path | |
|---|---|
| `ReanSDK/` | The plugin. Copy it to `YourProject/Plugins/`. |
| `ReanSDK/Source/ReanCore/` | Engine-independent C++17 (no Unreal headers): OAuth state machine, tokens, Social API, JSON, SHA-256, QR codes. |
| `ReanSDK/Source/ReanSDK/` | Runtime module: subsystem, Blueprint nodes, settings, HTTP transport, token stores. |
| `ReanSDK/Source/ReanSDKEditor/` | Editor module: settings check and Test sign-in. |
| `ReanCore/` | CMake build of the core, unit tests (doctest) and `rean_device_check`. |
| `Samples/ReanSample/` | Sample project (C++ UMG flow, plus a Blueprint guide). |

Build and test the core without Unreal (CMake 3.16+, any C++17 compiler):

```bash
cmake -S ReanCore -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The core is compiled the way Unreal compiles plugin code (no exceptions, no RTTI, warnings as errors). CI runs it on Windows (MSVC, C++17 and C++20), Linux (gcc, clang, AddressSanitizer + UBSan) and macOS, and checks the QR codes with two independent decoders.

**Check your app against the real API without Unreal** (needs libcurl):

```bash
cmake -S ReanCore -B build && cmake --build build --target rean_device_check
REAN_CLIENT_ID=rean_xxxxxxxxxxxxxxxxxxxxxxxx ./build/rean_device_check            # add --api https://rean-client-vyli.vercel.app if needed
```

It shows a device code and a QR code in the terminal, waits for you to approve it, then calls every Social API endpoint once and revokes the session. `--invite <friend-id>` also sends a test invite.

## License

MIT, see [LICENSE](LICENSE). Copyright (c) REAN Studios. Uses doctest (MIT) in tests only; the QR encoder follows Project Nayuki's QR Code generator (MIT).
