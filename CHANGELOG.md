# Changelog

All notable changes to the REAN SDK for Unreal Engine. Versions follow [Semantic Versioning](https://semver.org/).

## [0.1.0-preview] - Unreleased

**Preview: not yet compiled in UE 5.8.** `ReanCore` is built and tested in CI; the Unreal modules have not been compiled in Unreal Engine yet.

### Added
- `ReanCore`: engine-independent C++17 core (no Unreal headers), unit-tested with doctest on Windows, Linux and macOS.
  - Sign in with REAN: device code, REAN Launcher code (`-ReanAuthCode`, never sent twice), saved session, browser sign-in with PKCE S256, `state` and `nonce`.
  - Session handling: refresh a minute before expiry and once after a 401, never in parallel; rotating refresh tokens saved first; `invalid_grant` ends the session; late answers after cancel / sign-out are ignored and their orphaned sessions revoked.
  - Social API: me, friends with presence, presence lookup, rich presence with a 60 s heartbeat, invites, event polling with a persisted cursor.
  - QR code encoder for the device sign-in address.
  - `rean_device_check`: command-line device sign-in and Social API check against the real API.
- `ReanSDK` runtime module: `UReanSubsystem`, Blueprint async nodes (Sign In With Launcher / Device / Browser, Restore Session, Get Friends, Get Presence, Get Invites, Send Invite, Respond To Invite), Set / Clear Activity, Sign Out, events (On Invite Received, On Invite Answered, On Event, On Session Changed), Project Settings page, token stores (DPAPI on Windows, SaveGame elsewhere, memory, or your own `IReanTokenStore`).
- `ReanSDKEditor` module: client id check and Test sign-in button in Project Settings, `Rean.TestSignIn` console command.
- `Samples/ReanSample`: C++ UMG sample and a Blueprint walk-through.
