#pragma once

#include <functional>
#include <atomic>
#include <QFuture>
#include <QSet>
#include <QVariantList>

#include "sync/idlewatcher.h"
#include "sync/backgroundworker.h"

class AccountRepository;
class DataStore;
class TokenVault;
class QFutureWatcherBase;
class QTimer;
class QThread;

class ImapService : public QObject
{
    Q_OBJECT
public:
    explicit ImapService(AccountRepository *accounts, DataStore *store, TokenVault *vault, QObject *parent = nullptr);
    ~ImapService() override;

    Q_INVOKABLE void syncAll(bool announce = true);
    Q_INVOKABLE void syncFolder(const QString &folderName, bool announce = true);
    Q_INVOKABLE void refreshFolderList(bool announce = true);
    Q_INVOKABLE void hydrateMessageBody(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE void moveMessage(const QString &accountEmail, const QString &folder,
                                 const QString &uid, const QString &targetFolder);
    Q_INVOKABLE void markMessageRead(const QString &accountEmail, const QString &folder,
                                     const QString &uid);

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void shutdown();

signals:
    void syncFinished(bool ok, const QString &message);
    void syncActivityChanged(bool active);
    void hydrateStatus(bool ok, const QString &message);
    void realtimeStatus(bool ok, const QString &message);

private:
    // Internal result type for async sync work lambdas.
    struct SyncResult {
        bool ok = false;
        QString message;
        QVariantList headers;
        int inserted = 0;
    };

    AccountRepository *m_accounts   = nullptr;
    DataStore         *m_store      = nullptr;
    TokenVault        *m_vault      = nullptr;

    bool              m_syncInProgress = false;
    std::atomic_bool  m_cancelRequested { false };
    std::atomic_bool  m_destroying      { false };

    QMutex   m_pendingSyncMutex;
    QString  m_pendingFolderSync;
    bool     m_pendingFullSync  = false;
    bool     m_pendingAnnounce  = true;

    Imap::IdleWatcher     *m_idleWatcher = nullptr;
    QThread               *m_idleThread = nullptr;
    Imap::BackgroundWorker *m_backgroundWorker = nullptr;
    QThread               *m_backgroundThread = nullptr;
    QTimer                *m_syncTimer = nullptr;

    QSet<QFutureWatcherBase*> m_activeWatchers;
    QMutex                    m_activeWatchersMutex;

    struct AccountInfo { QString email, host, accessToken; int port = 0; };
    QList<AccountInfo> resolveAccounts(const QVariantList &accounts);

    void startIdleWatcher();
    void stopIdleWatcher(bool waitForStop = true);
    void startBackgroundWorker();
    void stopBackgroundWorker(bool waitForStop = true) const;

    QVariantList workerGetAccounts() const;
    QString workerRefreshAccessToken(const QVariantMap &account, const QString &email);

    QStringList idleGetFolderUids(const QString &email, const QString &folder);
    void idlePruneFolderToUids(const QString &email, const QString &folder, const QStringList &uids);
    void idleRemoveUids(const QString &email, const QStringList &uids);
    void idleOnInboxChanged();

    void workerEmitRealtimeStatus(bool ok, const QString &message);
    void backgroundOnLoopError(const QString &message);

    void backgroundLoginSessionStartup(const QVariantMap &account, const QString &email, const QString &accessToken);
    QStringList backgroundListFolders(const QVariantMap &account, const QString &email, const QString &accessToken);
    bool backgroundShouldSyncFolder(const QVariantMap &account, const QString &email,
                                    const QString &folder, const QString &accessToken);
    void backgroundSyncHeadersAndFlags(const QVariantMap &account, const QString &email,
                                       const QString &folder, const QString &accessToken);
    void backgroundFetchBodies(const QVariantMap &account, const QString &email,
                               const QString &folder, const QString &accessToken);
    void backgroundOnIdleLiveUpdate(const QVariantMap &account, const QString &email);

    QVariantMap loadFolderStatusSnapshot(const QString &accountEmail, const QString &folder) const;
    void saveFolderStatusSnapshot(const QString &accountEmail, const QString &folder,
                                  qint64 uidNext, qint64 highestModSeq, qint64 messages);

    void registerWatcher(QFutureWatcherBase *watcher);
    void unregisterWatcher(QFutureWatcherBase *watcher);
    void waitForActiveWatchers(int timeoutMs);

    void drainPendingSync();
    void runAsync(std::function<SyncResult()> work, std::function<void(const SyncResult&)> onDone);

    QString refreshAccessToken(const QVariantMap &account, const QString &email);

    QVariantList fetchFolderHeaders(const QString &host, int port,
                                   const QString &email, const QString &accessToken,
                                   QString *statusOut,
                                   const QString &folderName = QStringLiteral("INBOX"),
                                   const std::function<void(const QVariantMap &)> &onHeader = {},
                                   qint64 minUidExclusive = -1,
                                   bool reconcileDeletes = false,
                                   int fetchBudget = -1);
};
