# Kestrel Mail

> ⚠️ **Pre-alpha / heavy development:** Kestrel Mail is currently under heavy development and is **not usable for normal day-to-day email yet**.

Kestrel Mail is a KDE-native email client project focused on modern UX, reliability, and first-class OAuth2 integration for Gmail and Microsoft 365.

## Vision

- Beautiful, Outlook/eM Client-inspired tri-pane interface
- KDE-first UX with Kirigami
- Fast local search and conversation threading
- Reliable IMAP/SMTP core with offline sync

## Current Status

Project scaffold is in place (Qt 6 + Kirigami + CMake). Core mail functionality is next.

## Tech Stack

- Qt 6 + QML
- Kirigami
- C++20 backend
- SQLite (planned)
- OAuth2 PKCE for Gmail/M365 (planned)

## OAuth setup (Gmail / Microsoft 365)

Configure provider OAuth client credentials directly in provider profile data
(or account/provider settings in-app). Environment-variable based OAuth client
configuration is intentionally not supported.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/kestrel-mail
```

## License

GPL-3.0-or-later
