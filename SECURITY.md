# Security policy

## Reporting a vulnerability

Please report security problems privately, not in a public issue:

- Use GitHub's **Report a vulnerability** button (*Security › Advisories*) on this repository, or
- write to REAN Studios through the support page: https://devellopers-reanstudios-com.vercel.app/docs/support (ask for a private security contact).

Include what you found, how to reproduce it, and what an attacker could do with it. We will acknowledge your report, keep you informed, and credit you when the fix ships if you wish.

## Never post tokens

Do **not** paste access tokens, refresh tokens (`rrt_…`), launcher codes (`rlc_…`), device codes, client secrets or logs that contain them in issues, pull requests or discussions, even expired ones. If you posted one by mistake, sign out of the game (which revokes the session) or remove the app in *Account › Connected apps* on reanstudios.com, and delete the post.

Client ids (`rean_…`) are public, but please use the placeholder `rean_xxxxxxxxxxxxxxxxxxxxxxxx` in reports anyway.

## Scope

In scope: this SDK (token handling and storage, the OAuth flows, the loopback listener for browser sign-in, request building, parsing of REAN answers).

Problems in the REAN API or website themselves: report them the same way; we will route them.

## Design notes for reviewers

- Games are public OAuth clients: no client secret exists in the SDK.
- Refresh tokens rotate; reusing an old one makes REAN revoke the whole chain. The SDK saves the new token before anything else, and never sends a launcher code twice.
- Tokens are never logged. On Windows they are stored with DPAPI; on other platforms the default SaveGame store is unencrypted and logs a warning (games should provide secure storage).
- Browser sign-in uses PKCE S256, checks `state` and the ID token's `nonce` and `aud`, and listens on 127.0.0.1 only.
