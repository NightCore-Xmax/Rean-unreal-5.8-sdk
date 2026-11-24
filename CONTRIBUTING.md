# Contributing

Thanks for helping. Bug reports with compiler output from Unreal Engine are especially welcome while the plugin is in preview.

## Reporting a problem

Open an issue with: Unreal Engine version, platform, what you did, what happened (full compiler or log output, filtered by `LogRean`), and what you expected. Never include tokens (see [SECURITY.md](SECURITY.md)); use the placeholder client id `rean_xxxxxxxxxxxxxxxxxxxxxxxx`.

## Changing the code

- **Logic belongs in `ReanCore`** (`ReanSDK/Source/ReanCore`): plain C++17, no Unreal headers, no exceptions, no RTTI. Add or update tests in `ReanCore/tests` for every behaviour change, and run them:

  ```bash
  cmake -S ReanCore -B build && cmake --build build && ctest --test-dir build --output-on-failure
  ```

  Sanitizers: add `-DREAN_CORE_SANITIZE=ON` (gcc / clang).
- **The Unreal modules stay thin**: conversion to Unreal types, the HTTP transport, storage, Blueprint nodes, editor UI. Keep platform-specific calls in `ReanTokenStores.cpp`.
- Avoid identifiers Unreal defines as macros in `ReanCore` (`check`, `verify`, `ensure`, `TEXT`, `IN`, `OUT`) and Windows macro names (`GetMessage`, `GetObject`, `SendMessage`).
- Follow the existing style (Unreal-style names, tabs). Never commit client ids, tokens or local config; the CI checks for them.
- Behaviour must match the public docs at https://devellopers-reanstudios-com.vercel.app/docs and the REAN API. If you find a difference, describe it in the pull request.

By contributing you agree that your contributions are licensed under the MIT License of this repository.
