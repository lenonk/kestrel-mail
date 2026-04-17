# Kestrel Mail

> **Alpha / active development:** Kestrel Mail is under heavy development and is **usable for day-to-day email**, but not yet feature complete. Gmail (OAuth2) and generic IMAP (password auth) are both supported.

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
- Message actions: mark read/flagged, move, archive, delete (batch operations with chunked IMAP commands)
- Ctrl+A select-all with instant bulk delete (thread-aware edge cleanup)
- Drag-and-drop message moving between folders with visual feedback
- Drag-and-drop label/tag assignment (copy, not move) for Gmail labels
- Contact avatars with multi-source resolution and caching
- Threading via Message-ID / In-Reply-To / References headers
- Gmail category tabs (Primary, Social, Promotions, Updates, Forums)
- Local folders with full message + attachment archival (drag to copy from server)
- Scrollable sidebar with folder hierarchy (collapsible sections, favorites bar, tags, per-account "More" folders)
- Message list header with folder name and stats (total/unread), or Gmail category tabs
- Batch message loading (5k per page) with barber-pole progress indicator
- Search bar with full-text message search and contact lookup
- Compose window with rich text editing and SMTP send
- Desktop notifications with circular avatars, account indicator, and quick actions (Mark Read, Reply)
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
- Per-account IMAP service with dedicated connection pools and idle watchers
- Startup splash screen with real initialization (DB integrity check, connection pool establishment)
- OAuth2 with PKCE, automatic re-authentication on token expiry
- Token storage: KWallet (preferred), libsecret (GNOME), or plaintext file fallback
- SQLite with WAL mode, per-thread connections, and forward-compatible schema migrations
- Per-account throttle detection with multi-observer registry
- IMAP Lab tool for interactive protocol debugging
- Privacy policy for Google API compliance

## Tech Stack

- Qt 6 + QML (C++26)
- KDE Frameworks 6 (Kirigami, KI18n, KCoreAddons, KWallet, KNotifications)
- SQLite (WAL mode)
- OAuth2 (PKCE)
- QtWebEngine (message rendering)
- Botan 3 (cryptography)

## Dependencies

### Build tools
- CMake 3.27+
- Clang (preferred) or GCC with C++26 support
- Extra CMake Modules (ECM)

### Qt 6 modules
- Qt6 Core, Gui, Qml, Quick, Sql, Network, Concurrent, WebEngineQuick, Widgets

### KDE Frameworks 6
- KF6 CoreAddons
- KF6 I18n (KI18n)
- KF6 Notifications (KNotifications)
- KF6 Wallet (KWallet)
- KF6 Kirigami
- KF6 KirigamiPlatform

### Libraries
- Botan 3 (`botan-3` via pkg-config)
- libsecret-1 (optional, for GNOME keyring token storage)

### Arch Linux / CachyOS

```bash
sudo pacman -S cmake extra-cmake-modules clang \
    qt6-base qt6-declarative qt6-webengine qt6-webchannel \
    kirigami ki18n kcoreaddons kwallet knotifications \
    botan libsecret
```

### Fedora / openSUSE

```bash
sudo dnf install cmake extra-cmake-modules clang \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwebengine-devel \
    qt6-qtwebchannel-devel \
    kf6-kirigami-devel kf6-ki18n-devel kf6-kcoreaddons-devel \
    kf6-kwallet-devel kf6-knotifications-devel \
    botan2-devel libsecret-devel
```

### Ubuntu / Debian (24.04+)

```bash
sudo apt install cmake extra-cmake-modules clang \
    qt6-base-dev qt6-declarative-dev qt6-webengine-dev \
    qt6-webchannel-dev \
    libkf6kirigami-dev libkf6i18n-dev libkf6coreaddons-dev \
    libkf6wallet-dev libkf6notifications-dev \
    libbotan-2-dev libsecret-1-dev
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/kestrel-mail
```

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
