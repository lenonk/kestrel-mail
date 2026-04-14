# Multi-Account Architecture Design

## Problem Statement

Kestrel Mail was built around a single-account assumption. Adding a second account
causes folder lists to mix, syncs to try wrong folders on wrong servers, and the UI
to break because everything shares a single flat namespace.

## Design Principles

1. **Account is a first-class object** — not a row in a JSON file
2. **Polymorphic** — Gmail, IMAP, POP3 (future) share an interface, differ in implementation
3. **The UI is account-type-agnostic** — it calls `account->folderList()` and gets the right thing
4. **Each account owns its own state** — folders, tags, sync, connection, status
5. **No quick fixes or hacks** — implement correctly from the start

## Interface: `IAccount`

```cpp
class IAccount : public QObject {
    Q_OBJECT

    // ── Identity ──────────────────────────────────────────────────
    Q_PROPERTY(QString email READ email CONSTANT)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountNameChanged)
    Q_PROPERTY(QString avatarSource READ avatarSource NOTIFY avatarSourceChanged)

    // ── Folders & Tags ────────────────────────────────────────────
    Q_PROPERTY(QVariantList folderList READ folderList NOTIFY foldersChanged)
    Q_PROPERTY(QVariantList tagList READ tagList NOTIFY tagsChanged)
    Q_PROPERTY(QStringList categoryTabs READ categoryTabs NOTIFY categoryTabsChanged)

    // ── Status ────────────────────────────────────────────────────
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool throttled READ throttled NOTIFY throttledChanged)
    Q_PROPERTY(bool needsReauth READ needsReauth NOTIFY needsReauthChanged)

public:
    // ── Identity ──────────────────────────────────────────────────
    virtual QString email() const = 0;
    virtual QString displayName() const = 0;
    virtual QString accountName() const = 0;
    virtual QString avatarSource() const = 0;

    // ── Folders & Tags ────────────────────────────────────────────
    virtual QVariantList folderList() const = 0;      // This account's folders only
    virtual QVariantList tagList() const = 0;          // This account's tags/labels only
    virtual QStringList categoryTabs() const = 0;      // Gmail: [Primary, Social, ...]; IMAP: []
    virtual QVariantMap folderStats(const QString &folderKey) const = 0;

    // ── Sync ──────────────────────────────────────────────────────
    virtual void syncAll() = 0;
    virtual void syncFolder(const QString &folderName) = 0;
    virtual void refreshFolderList() = 0;

    // ── Connection ────────────────────────────────────────────────
    virtual void initialize() = 0;                     // Create pool, start IDLE
    virtual void shutdown() = 0;
    virtual void reauthenticate() = 0;

    // ── Messages (scoped to this account) ─────────────────────────
    virtual QVariantList messagesForFolder(const QString &folder, int limit, int offset) const = 0;
    virtual QVariantMap messageByKey(const QString &folder, const QString &uid) const = 0;
    virtual void hydrateMessageBody(const QString &folder, const QString &uid) = 0;

signals:
    void displayNameChanged();
    void accountNameChanged();
    void avatarSourceChanged();
    void foldersChanged();
    void tagsChanged();
    void categoryTabsChanged();
    void connectedChanged();
    void syncingChanged();
    void throttledChanged();
    void needsReauthChanged();
    void syncFinished(bool ok, const QString &message);
    void bodyHtmlUpdated(const QString &folder, const QString &uid);
    void newMailReceived(const QVariantMap &info);
};
```

## Implementations

### `GmailAccount : IAccount`
- Uses XOAUTH2 authentication
- Has Gmail-specific folder structure ([Gmail]/All Mail, categories)
- Category tabs: [Primary, Promotions, Social, ...]
- Tags from Gmail labels
- IDLE on INBOX
- Google Calendar, Contacts integration (account-scoped)

### `ImapAccount : IAccount`
- Uses LOGIN (password) authentication
- Standard IMAP folder hierarchy
- Category tabs: empty (no Gmail categories)
- Tags from IMAP folders (or none)
- IDLE on INBOX (if server supports it)
- No calendar/contacts integration

### Future: `Pop3Account : IAccount`
- Download-only, no server-side folders
- Local folder management
- No IDLE

## `AccountManager` — The Container

```cpp
class AccountManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<IAccount*> accounts READ accounts NOTIFY accountsChanged)

public:
    QList<IAccount*> accounts() const;
    IAccount* accountByEmail(const QString &email) const;

    // Creates the right implementation based on account config
    IAccount* addAccount(const QVariantMap &config);
    void removeAccount(const QString &email);

signals:
    void accountsChanged();
};
```

On startup, `AccountManager` reads `AccountRepository`, creates the appropriate
`IAccount` subclass for each entry, and initializes them. The QML binds to
`accountManager.accounts`.

## How the UI Uses It

```qml
// FolderPane.qml
Repeater {
    model: accountManager.accounts
    delegate: AccountSection {
        required property var modelData
        readonly property IAccount account: modelData

        sectionTitle: account.accountName
        sectionIcon: account.avatarSource
        syncing: account.syncing
        connected: account.connected

        folderModel: account.folderList
        tagModel: account.tagList
        categoryTabs: account.categoryTabs
    }
}
```

The delegate doesn't know if it's rendering a Gmail account or an IMAP account.
It just reads properties and calls methods through the interface.

## What Each Implementation Owns

| Responsibility | GmailAccount | ImapAccount |
|---------------|-------------|------------|
| Auth | XOAUTH2, token refresh | LOGIN, password from vault |
| Folder list | XLIST/LIST + Gmail defaults | LIST |
| Category tabs | Derived from Gmail labels | Empty |
| Tags/labels | Gmail labels via X-GM-LABELS | IMAP folders (optional) |
| IDLE | INBOX on imap.gmail.com | INBOX on account host |
| Sync strategy | Gmail-aware (categories, All Mail) | Standard IMAP |
| Connection pool | Pool slots for this account | Pool slots for this account |
| Calendar | Google Calendar API | None |
| Contacts | Google People API | None |

## Migration Path

### Phase 1: IAccount interface + GmailAccount (no behavior change)
- Create the interface and GmailAccount implementation
- GmailAccount wraps the existing ImapService/DataStore calls, scoped by email
- AccountManager replaces the current flat account iteration
- The UI still works exactly as before — just one account in the list
- **Test: single Gmail account behaves identically**

### Phase 2: ImapAccount implementation
- Create ImapAccount with LOGIN auth, standard LIST, no categories
- AccountManager creates ImapAccount for `authType: "password"` accounts
- Connection pool supports multiple accounts (already partially done)
- **Test: sanctuary.org appears as a separate section with its own folders**

### Phase 3: Move sync logic into account implementations
- GmailAccount.syncAll() does Gmail-specific sync (categories, All Mail, labels)
- ImapAccount.syncAll() does standard IMAP sync (all LIST folders)
- ImapService becomes a utility/connection-pool manager, not an orchestrator
- **Test: syncing one account doesn't touch the other's folders**

### Phase 4: Per-account IDLE
- Each account owns its own IdleWatcher (or equivalent)
- GmailAccount IDLEs on imap.gmail.com
- ImapAccount IDLEs on its server
- **Test: new mail notification works for both accounts independently**

### Phase 5: Per-account settings/UI
- Account-specific sync period, offline download, download scope
- Account avatar/icon in folder pane
- Account-specific reauth flow

## What Already Works (Keep)

- SQLite schema — already has `account_email` on all tables
- FolderKey parser — `account:<email>:<folder>` format
- Query-layer account filters — `messagesForFolderView` etc.
- Connection LOGIN auth — works for password accounts
- TokenVault password storage — works across all backends
- LIST parser fix — handles both Gmail XLIST and standard LIST
- Per-account folder cache in SyncEngine
- Auto-discovery (MX + CAPABILITY probe)

## What to Revert/Redo

- FolderPane.qml — revert to single-account, rebuild on top of AccountManager
- Main.qml folder helpers — revert, replace with AccountManager bindings
- FolderUtils.js — keep the key format helpers, revert the account filtering
  (AccountModel.folderList() handles filtering internally)
- ImapService.syncAll — will be replaced by per-account sync
