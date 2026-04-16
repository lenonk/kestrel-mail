#include "baseaccount.h"

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

BaseAccount::BaseAccount(const QVariantMap &config, DataStore *store,
                         ImapService *imap, TokenVault *vault,
                         QObject *parent)
    : IAccount(parent), m_config(config), m_store(store), m_imap(imap), m_vault(vault)
    , m_email(Kestrel::normalizeEmail(config.value("email"_L1).toString()))
{
    if (m_store) {
        connect(m_store, &DataStore::foldersChanged, this, &BaseAccount::foldersChanged);
        connect(m_store, &DataStore::bodyHtmlUpdated, this, [this](const QString &acct, const QString &folder, const QString &uid) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0)
                emit bodyHtmlUpdated(folder, uid);
        });
    }
    if (m_imap) {
        connect(m_imap, &ImapService::syncActivityChanged, this, [this](bool active) {
            updateSyncState(active);
        });
        connect(m_imap, &ImapService::syncFinished, this, [this](bool ok, const QString &msg) {
            emit syncFinished(ok, msg);
        });
        connect(m_imap, &ImapService::accountNeedsReauth, this, [this](const QString &acct) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_needsReauth = true; emit needsReauthChanged();
            }
        });
    }
}

BaseAccount::~BaseAccount() { shutdown(); }

// -- Identity -----------------------------------------------------------------

QString
BaseAccount::email() const { return m_email; }

QString
BaseAccount::displayName() const {
    return m_config.value("displayName"_L1).toString().trimmed();
}

QString
BaseAccount::accountName() const {
    const auto name = m_config.value("accountName"_L1).toString().trimmed();
    return name.isEmpty() ? m_email : name;
}

QString
BaseAccount::avatarSource() const {
    const auto custom = m_config.value("avatarIcon"_L1).toString().trimmed();
    if (!custom.isEmpty()) return custom;
    return "qrc:/qml/images/account-avatars/avatar-01.svg"_L1;
}

QString
BaseAccount::providerIcon() const { return "mail-message"_L1; }

QString
BaseAccount::providerId() const { return m_config.value("providerId"_L1).toString().toLower(); }

// -- Folders & Tags -----------------------------------------------------------

QVariantList
BaseAccount::folderList() const {
    if (!m_store) return {};
    QVariantList filtered;
    for (const auto &v : m_store->folders()) {
        if (v.toMap().value("accountEmail"_L1).toString().compare(m_email, Qt::CaseInsensitive) == 0)
            filtered.push_back(v);
    }
    return filtered;
}

QVariantList
BaseAccount::tagList() const { return {}; }

QStringList
BaseAccount::categoryTabs() const { return {}; }

QVariantMap
BaseAccount::folderStats(const QString &folderKey) const {
    if (!m_store) return {};
    return m_store->statsForFolder(folderKey, FolderKey::parseAccountKey(folderKey).folder);
}

// -- Sync targets -------------------------------------------------------------

QStringList
BaseAccount::syncTargets() const {
    if (!m_imap) return { "INBOX"_L1 };
    return m_imap->syncTargetsForAccount(m_email, m_config.value("imapHost"_L1).toString());
}

// -- Sync ---------------------------------------------------------------------

void
BaseAccount::syncAll() {
    if (!m_imap) return;
    refreshFolderList();
    for (const auto &folder : syncTargets())
        syncFolder(folder);
}

void
BaseAccount::syncFolder(const QString &folderName) {
    if (!m_imap) return;
    m_imap->syncFolder(folderName, false, m_email);
}

void
BaseAccount::refreshFolderList() {
    if (!m_imap) return;
    m_imap->refreshFolderList(false);
}

// -- Connection ---------------------------------------------------------------

void
BaseAccount::initialize() {
    startIdleWatcher();
    startBackgroundWorker();
}

void
BaseAccount::shutdown() {
    stopIdleWatcher();
    stopBackgroundWorker();
}

void
BaseAccount::reauthenticate() {
    m_needsReauth = false;
    emit needsReauthChanged();
}

// -- Status -------------------------------------------------------------------

bool BaseAccount::connected() const { return m_connected; }
bool BaseAccount::syncing() const { return m_syncing; }
bool BaseAccount::throttled() const { return m_throttled; }
bool BaseAccount::needsReauth() const { return m_needsReauth; }

// -- Private ------------------------------------------------------------------

void
BaseAccount::updateSyncState(const bool active) {
    if (active) {
        m_syncCount++;
        if (!m_syncing) { m_syncing = true; emit syncingChanged(); }
    } else {
        m_syncCount = qMax(0, m_syncCount - 1);
        if (m_syncCount == 0)
            QTimer::singleShot(500, this, [this]() {
                if (m_syncCount == 0 && m_syncing) { m_syncing = false; emit syncingChanged(); }
            });
    }
}

void
BaseAccount::startIdleWatcher() {
    if (m_idleWatcher) return;

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

    if (m_imap) m_imap->wireIdleWatcher(m_idleWatcher, m_email);

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
BaseAccount::stopIdleWatcher() {
    if (m_syncTimer) { m_syncTimer->stop(); m_syncTimer->deleteLater(); m_syncTimer = nullptr; }
    if (!m_idleWatcher || !m_idleThread) return;
    m_idleWatcher->stop();
    m_idleThread->quit();
    while (!m_idleThread->wait(100))
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
}

void
BaseAccount::startBackgroundWorker() {
    if (m_bgWorker) return;

    m_bgThread = new QThread(this);
    m_bgWorker = new Imap::BackgroundWorker();
    if (m_imap) m_bgWorker->setConnectionPool(m_imap->pool());
    m_bgWorker->moveToThread(m_bgThread);

    connect(m_bgThread, &QThread::started, m_bgWorker, &Imap::BackgroundWorker::start);

    if (m_imap) m_imap->wireBackgroundWorker(m_bgWorker, m_email);

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
BaseAccount::stopBackgroundWorker() {
    if (!m_bgWorker || !m_bgThread) return;
    m_bgWorker->stop();
    m_bgThread->quit();
    m_bgThread->wait(2000);
}
