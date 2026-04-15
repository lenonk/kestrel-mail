#include "gmailaccount.h"

#include "../store/datastore.h"
#include "../store/folderkey.h"
#include "../transport/imap/imapservice.h"
#include "../transport/imap/sync/idlewatcher.h"
#include "../transport/imap/sync/backgroundworker.h"
#include "../auth/tokenvault.h"
#include "../utils.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

using namespace Qt::Literals::StringLiterals;

GmailAccount::GmailAccount(const QVariantMap &config, DataStore *store,
                             ImapService *imap, TokenVault *vault,
                             QObject *parent)
    : IAccount(parent), m_config(config), m_store(store), m_imap(imap), m_vault(vault)
    , m_email(Kestrel::normalizeEmail(config.value("email"_L1).toString()))
{
    if (m_store) {
        connect(m_store, &DataStore::foldersChanged, this, &GmailAccount::foldersChanged);
        connect(m_store, &DataStore::bodyHtmlUpdated, this, [this](const QString &acct, const QString &folder, const QString &uid) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0)
                emit bodyHtmlUpdated(folder, uid);
        });
    }
    if (m_imap) {
        connect(m_imap, &ImapService::accountSyncActivity, this, [this](const QString &acct, bool active) {
            if (acct.compare(m_email, Qt::CaseInsensitive) != 0) return;
            updateSyncState(active);
        });
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

GmailAccount::~GmailAccount() {
    shutdown();
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
    const auto custom = m_config.value("avatarIcon"_L1).toString().trimmed();
    if (!custom.isEmpty()) return custom;
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
    const auto rawFolder = FolderKey::parseAccountKey(folderKey).folder;
    return m_store->statsForFolder(folderKey, rawFolder);
}

// ── Sync targets ─────────────────────────────────────────────────────────────

QStringList
GmailAccount::syncTargets() const {
    if (!m_imap) return { "INBOX"_L1 };
    return m_imap->syncTargetsForAccount(m_email,
        m_config.value("imapHost"_L1).toString());
}

// ── Sync ─────────────────────────────────────────────────────────────────────

void
GmailAccount::syncAll() {
    if (!m_imap) return;
    refreshFolderList();
    const auto targets = syncTargets();
    for (const auto &folder : targets)
        syncFolder(folder);
}

void
GmailAccount::syncFolder(const QString &folderName) {
    if (!m_imap) return;
    m_imap->syncFolder(folderName, false, m_email);
}

void
GmailAccount::refreshFolderList() {
    if (!m_imap) return;
    m_imap->refreshFolderList(false);
}

// ── Connection ───────────────────────────────────────────────────────────────

void
GmailAccount::initialize() {
    startIdleWatcher();
    startBackgroundWorker();
}

void
GmailAccount::shutdown() {
    stopIdleWatcher();
    stopBackgroundWorker();
}

void
GmailAccount::reauthenticate() {
    m_needsReauth = false;
    emit needsReauthChanged();
}

// ── Status ───────────────────────────────────────────────────────────────────

bool GmailAccount::connected() const { return m_connected; }
bool GmailAccount::syncing() const { return m_syncing; }
bool GmailAccount::throttled() const { return m_throttled; }
bool GmailAccount::needsReauth() const { return m_needsReauth; }

// ── Private: sync state ──────────────────────────────────────────────────────

void
GmailAccount::updateSyncState(const bool active) {
    if (active) {
        m_syncCount++;
        if (!m_syncing) {
            m_syncing = true;
            emit syncingChanged();
        }
    } else {
        m_syncCount = qMax(0, m_syncCount - 1);
        if (m_syncCount == 0) {
            QTimer::singleShot(500, this, [this]() {
                if (m_syncCount == 0 && m_syncing) {
                    m_syncing = false;
                    emit syncingChanged();
                }
            });
        }
    }
}

// ── Private: per-account IDLE watcher ────────────────────────────────────────

void
GmailAccount::startIdleWatcher() {
    if (m_idleWatcher) return;

    // Periodic sync timer.
    if (!m_syncTimer) {
        m_syncTimer = new QTimer(this);
        m_syncTimer->setInterval(120'000);
        connect(m_syncTimer, &QTimer::timeout, this, [this]() { syncAll(); });
        m_syncTimer->start();
    }

    m_idleThread = new QThread(this);
    m_idleWatcher = new Imap::IdleWatcher();
    m_idleWatcher->moveToThread(m_idleThread);

    connect(m_idleThread, &QThread::started, m_idleWatcher, &Imap::IdleWatcher::start);

    // Wire the idle watcher signals through ImapService's existing handlers.
    // The watcher needs accounts, tokens, and DB access — all on the main thread.
    if (m_imap)
        m_imap->wireIdleWatcher(m_idleWatcher, m_email);

    // When IDLE detects inbox change, sync this account's inbox.
    connect(m_idleWatcher, &Imap::IdleWatcher::inboxChanged,
            this, [this]() { syncFolder("INBOX"_L1); }, Qt::QueuedConnection);

    connect(m_idleThread, &QThread::finished, m_idleWatcher, &QObject::deleteLater);
    connect(m_idleThread, &QThread::finished, this, [this]() {
        m_idleWatcher = nullptr;
        if (m_idleThread) { m_idleThread->deleteLater(); m_idleThread = nullptr; }
    });

    m_idleThread->start();
}

void
GmailAccount::stopIdleWatcher() {
    if (m_syncTimer) {
        m_syncTimer->stop();
        m_syncTimer->deleteLater();
        m_syncTimer = nullptr;
    }
    if (!m_idleWatcher || !m_idleThread) return;
    m_idleWatcher->stop();
    m_idleThread->quit();
    while (!m_idleThread->wait(100))
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
}

// ── Private: per-account background worker ───────────────────────────────────

void
GmailAccount::startBackgroundWorker() {
    if (m_bgWorker) return;

    m_bgThread = new QThread(this);
    m_bgWorker = new Imap::BackgroundWorker();
    m_bgWorker->moveToThread(m_bgThread);

    connect(m_bgThread, &QThread::started, m_bgWorker, &Imap::BackgroundWorker::start);

    // Wire the background worker signals through ImapService.
    if (m_imap)
        m_imap->wireBackgroundWorker(m_bgWorker, m_email);

    // When background worker requests a folder sync, route through this account.
    connect(m_bgWorker, &Imap::BackgroundWorker::syncHeadersAndFlagsRequested,
            this, [this](const QVariantMap &, const QString &, const QString &folder, const QString &) {
                syncFolder(folder);
            }, Qt::QueuedConnection);

    connect(m_bgThread, &QThread::finished, m_bgWorker, &QObject::deleteLater);
    connect(m_bgThread, &QThread::finished, this, [this]() {
        m_bgWorker = nullptr;
        if (m_bgThread) { m_bgThread->deleteLater(); m_bgThread = nullptr; }
    });

    m_bgThread->start();
}

void
GmailAccount::stopBackgroundWorker() {
    if (!m_bgWorker || !m_bgThread) return;
    m_bgWorker->stop();
    m_bgThread->quit();
    m_bgThread->wait(2000);
}
