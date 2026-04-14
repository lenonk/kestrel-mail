#include "imapaccount.h"

#include "../store/datastore.h"
#include "../store/folderkey.h"
#include "../transport/imap/imapservice.h"
#include "../auth/tokenvault.h"
#include "../utils.h"

using namespace Qt::Literals::StringLiterals;

ImapAccount::ImapAccount(const QVariantMap &config, DataStore *store,
                           ImapService *imap, TokenVault *vault,
                           QObject *parent)
    : IAccount(parent), m_config(config), m_store(store), m_imap(imap), m_vault(vault)
    , m_email(Kestrel::normalizeEmail(config.value("email"_L1).toString()))
{
    if (m_store) {
        connect(m_store, &DataStore::foldersChanged, this, &ImapAccount::foldersChanged);
        connect(m_store, &DataStore::bodyHtmlUpdated, this, [this](const QString &acct, const QString &folder, const QString &uid) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0)
                emit bodyHtmlUpdated(folder, uid);
        });
    }
    if (m_imap) {
        connect(m_imap, &ImapService::syncFinished, this, [this](bool ok, const QString &msg) {
            if (m_syncing) {
                m_syncing = false;
                emit syncingChanged();
            }
            emit syncFinished(ok, msg);
        });
        connect(m_imap, &ImapService::accountNeedsReauth, this, [this](const QString &acct) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_needsReauth = true;
                emit needsReauthChanged();
            }
        });
    }
}

// ── Identity ─────────────────────────────────────────────────────────────────

QString ImapAccount::email() const { return m_email; }

QString
ImapAccount::displayName() const {
    return m_config.value("displayName"_L1).toString().trimmed();
}

QString
ImapAccount::accountName() const {
    const auto name = m_config.value("accountName"_L1).toString().trimmed();
    return name.isEmpty() ? m_email : name;
}

QString
ImapAccount::avatarSource() const {
    const auto custom = m_config.value("avatarIcon"_L1).toString().trimmed();
    if (!custom.isEmpty()) return custom;
    return "qrc:/qml/images/account-avatars/avatar-01.svg"_L1;
}

QString
ImapAccount::providerIcon() const {
    return "mail-message"_L1;
}

// ── Folders & Tags ───────────────────────────────────────────────────────────

QVariantList
ImapAccount::folderList() const {
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
ImapAccount::tagList() const {
    // Generic IMAP has no label/tag system.
    return {};
}

QStringList
ImapAccount::categoryTabs() const {
    // No Gmail-style categories.
    return {};
}

QVariantMap
ImapAccount::folderStats(const QString &folderKey) const {
    if (!m_store) return {};
    const auto rawFolder = FolderKey::parseAccountKey(folderKey).folder;
    return m_store->statsForFolder(folderKey, rawFolder);
}

// ── Sync ─────────────────────────────────────────────────────────────────────

void
ImapAccount::syncAll() {
    if (!m_imap) return;
    m_syncing = true;
    emit syncingChanged();
    m_imap->syncAll(true);
}

void
ImapAccount::syncFolder(const QString &folderName) {
    if (!m_imap) return;
    m_syncing = true;
    emit syncingChanged();
    m_imap->syncFolder(folderName, true, m_email);
}

void
ImapAccount::refreshFolderList() {
    if (!m_imap) return;
    m_imap->refreshFolderList();
}

// ── Connection ───────────────────────────────────────────────────────────────

void ImapAccount::initialize() {}
void ImapAccount::shutdown() {}

void
ImapAccount::reauthenticate() {
    m_needsReauth = false;
    emit needsReauthChanged();
}

// ── Status ───────────────────────────────────────────────────────────────────

bool ImapAccount::connected() const { return m_connected; }
bool ImapAccount::syncing() const { return m_syncing; }
bool ImapAccount::throttled() const { return m_throttled; }
bool ImapAccount::needsReauth() const { return m_needsReauth; }
