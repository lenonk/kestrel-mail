#include "gmailaccount.h"

#include "../store/datastore.h"
#include "../store/folderkey.h"
#include "../transport/imap/imapservice.h"
#include "../auth/tokenvault.h"
#include "../utils.h"

using namespace Qt::Literals::StringLiterals;

GmailAccount::GmailAccount(const QVariantMap &config, DataStore *store,
                             ImapService *imap, TokenVault *vault,
                             QObject *parent)
    : IAccount(parent), m_config(config), m_store(store), m_imap(imap), m_vault(vault)
    , m_email(Kestrel::normalizeEmail(config.value("email"_L1).toString()))
{
    // Forward relevant signals from ImapService/DataStore, filtered for this account.
    if (m_store) {
        connect(m_store, &DataStore::foldersChanged, this, &GmailAccount::foldersChanged);
        connect(m_store, &DataStore::bodyHtmlUpdated, this, [this](const QString &acct, const QString &folder, const QString &uid) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0)
                emit bodyHtmlUpdated(folder, uid);
        });
    }
    if (m_imap) {
        connect(m_imap, &ImapService::syncFinished, this, [this](bool ok, const QString &msg) {
            m_syncing = false;
            emit syncingChanged();
            emit syncFinished(ok, msg);
        });
        connect(m_imap, &ImapService::accountNeedsReauth, this, [this](const QString &acct) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_needsReauth = true;
                emit needsReauthChanged();
            }
        });
        connect(m_imap, &ImapService::accountThrottled, this, [this](const QString &acct, const QString &) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_throttled = true;
                emit throttledChanged();
            }
        });
        connect(m_imap, &ImapService::accountUnthrottled, this, [this](const QString &acct) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_throttled = false;
                emit throttledChanged();
            }
        });
    }
}

// ── Identity ─────────────────────────────────────────────────────────────────

QString GmailAccount::email() const { return m_email; }

QString
GmailAccount::displayName() const {
    return m_config.value("displayName"_L1).toString().trimmed();
}

QString
GmailAccount::accountName() const {
    const auto name = m_config.value("accountName"_L1).toString().trimmed();
    return name.isEmpty() ? m_email : name;
}

QString
GmailAccount::avatarSource() const {
    return "qrc:/qml/images/gmail_account_icon.svg"_L1;
}

QString
GmailAccount::providerIcon() const {
    return "mail-message"_L1;
}

// ── Folders & Tags ───────────────────────────────────────────────────────────

QVariantList
GmailAccount::folderList() const {
    if (!m_store) return {};
    QVariantList filtered;
    const auto all = m_store->folders();
    for (const auto &v : all) {
        if (v.toMap().value("accountEmail"_L1).toString().compare(m_email, Qt::CaseInsensitive) == 0)
            filtered.push_back(v);
    }
    return filtered;
}

QVariantList
GmailAccount::tagList() const {
    // Gmail labels as tags — currently derived from folders in the QML layer.
    // TODO: expose Gmail labels as first-class tags here.
    return {};
}

QStringList
GmailAccount::categoryTabs() const {
    if (!m_store) return { "Primary"_L1 };
    return m_store->inboxCategoryTabs();
}

QVariantMap
GmailAccount::folderStats(const QString &folderKey) const {
    if (!m_store) return {};
    // The folderKey already contains the account email in the new format.
    const auto rawFolder = FolderKey::parseAccountKey(folderKey).folder;
    return m_store->statsForFolder(folderKey, rawFolder);
}

// ── Sync ─────────────────────────────────────────────────────────────────────

void
GmailAccount::syncAll() {
    if (!m_imap) return;
    m_syncing = true;
    emit syncingChanged();
    // TODO: scope syncAll to this account only (Phase 3)
    m_imap->syncAll(true);
}

void
GmailAccount::syncFolder(const QString &folderName) {
    if (!m_imap) return;
    m_syncing = true;
    emit syncingChanged();
    m_imap->syncFolder(folderName, true, m_email);
}

void
GmailAccount::refreshFolderList() {
    if (!m_imap) return;
    // TODO: scope to this account only (Phase 3)
    m_imap->refreshFolderList();
}

// ── Connection ───────────────────────────────────────────────────────────────

void GmailAccount::initialize() {
    // Connection pool and IDLE are still managed globally by ImapService.
    // This will be moved into the account in Phase 3.
}

void GmailAccount::shutdown() {}

void
GmailAccount::reauthenticate() {
    m_needsReauth = false;
    emit needsReauthChanged();
    // TODO: trigger OAuth re-auth flow for this account
}

// ── Status ───────────────────────────────────────────────────────────────────

bool GmailAccount::connected() const { return m_connected; }
bool GmailAccount::syncing() const { return m_syncing; }
bool GmailAccount::throttled() const { return m_throttled; }
bool GmailAccount::needsReauth() const { return m_needsReauth; }
