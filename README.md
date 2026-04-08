# Kestrel Mail

> **Alpha / active development:** Kestrel Mail is under heavy development and is **usable for day-to-day email**, but not yet feature complete. It currently targets Gmail via OAuth2; other IMAP providers are untested.

Kestrel Mail is a KDE-native email client focused on modern UX, reliable IMAP sync, and OAuth2-first account setup.

## Vision

- Beautiful tri-pane interface inspired by desktop-first mail clients
- KDE-first UX with Kirigami
- Reliable IMAP core with incremental sync and local persistence
- OAuth2-driven account setup for major providers
- Integrated Google Calendar with invite management

## Current Status

Kestrel has active end-to-end mail, calendar, and local storage plumbing in place:

**Mail**
- Qt 6 + Kirigami app shell with split-pane mail layout
- Gmail OAuth2 flow (XOAUTH2 over IMAP) with automatic token refresh and re-authentication
- IMAP IDLE for real-time inbox updates
- CONDSTORE-based incremental sync for efficient folder updates
- Folder discovery + persistence in SQLite (WAL mode, per-thread connections)
- Background folder refresh worker with connection pooling (hydrate + operational slots)
- Full message body hydration with HTML rendering (WebEngineView)
- Inline image loading with sender trust controls and tracking pixel detection
- Attachment viewing, saving, and prefetching (BODYSTRUCTURE-based)
- Message actions: mark read/flagged, move, archive, delete
- Drag-and-drop message moving between folders with visual feedback
- Drag-and-drop label/tag assignment (copy, not move) for Gmail labels
- Contact avatars with multi-source resolution and caching
- Threading via Message-ID / In-Reply-To / References headers
- Gmail category tabs (Primary, Social, Promotions, Updates, Forums)
- Local folders with full message + attachment archival (drag to copy from server)
- Sidebar hierarchy (collapsible sections, favorites bar, tags)
- Search bar with full-text message search and contact lookup
- Compose window with rich text editing and SMTP send
- Desktop notifications with circular avatars and quick actions (Mark Read, Reply)
- System tray integration

**Calendar**
- Google Calendar integration (read/write scope)
- Week view with overlap-aware event layout (side-by-side sub-columns)
- Event cards with calendar-colored backgrounds, time, location, recurrence, and privacy indicators
- All-day event gutter
- Calendar sidebar with per-calendar visibility toggles
- Mini month calendar
- Live invite cards with Accept/Decline/Tentative RSVP via Google Calendar API
- 5-week event fetch window (ready for month view)

**Infrastructure**
- Startup splash screen with real initialization (DB integrity check, connection pool establishment)
- OAuth2 with PKCE, automatic re-authentication on token expiry
- Token storage: KWallet (preferred), libsecret (GNOME), or plaintext file fallback
- SQLite with WAL mode, per-thread connections, and forward-compatible schema migrations
- IMAP Lab tool for interactive protocol debugging
- Privacy policy for Google API compliance

## Tech Stack

- Qt 6 + QML
- KDE Kirigami
- C++20 backend
- SQLite (WAL)
- OAuth2 (PKCE)
- QtWebEngine (message rendering)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/kestrel-mail
```

Requires Qt 6, KDE Frameworks 6 (Kirigami, KI18n, KCoreAddons, KWallet), and QtWebEngine.

## Screenshots

### Mail workspace

![Mail workspace](Screenshots/Screenshot_20260319_025453.png)

### Calendar workspace

![Calendar workspace](Screenshots/Screenshot_20260319_025536.png)

### Inline images and attachments

![Inline images and attachments](Screenshots/Screenshot_20260319_025622.png)

### Demo screencast

![Demo screencast](Screenshots/Screencast_20260319_025819.gif)

## Legal

- [Privacy Policy](docs/PRIVACY_POLICY.md)
- [Terms of Service](docs/TERMS_OF_SERVICE.md)
- License: GPL-3.0-or-later
