#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <QFuture>
#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <QVariantList>

#include "sync/idlewatcher.h"
#include "sync/backgroundworker.h"

namespace Imap {
class Connection;
}
#include "connection/connectionpool.h"

class DataStore;
class TokenVault;
class QFutureWatcherBase;
class QTimer;
class QThread;
class QElapsedTimer;

class ImapService : public QObject
{
    Q_OBJECT
public:
    // Per-account constructor — each account owns its own ImapService instance.
    ImapService(const QVariantMap &accountConfig, DataStore *store, TokenVault *vault,
                QObject *parent = nullptr);

    // Global facade constructor — QML-facing, routes to per-account pools.
    explicit ImapService(DataStore *store, TokenVault *vault, QObject *parent = nullptr);
    ~ImapService() override;

    [[nodiscard]] Imap::ConnectionPool* pool() const { return m_pool.get(); }
    Q_INVOKABLE void syncFolder(const QString &folderName, bool announce = true, const QString &accountEmail = {});
    Q_INVOKABLE void refreshFolderList(bool announce = true);
    Q_INVOKABLE void hydrateMessageBody(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE void moveMessages(const QString &accountEmail, const QString &folder,
                                  const QStringList &uids, const QString &targetFolder);
    Q_INVOKABLE void expungeMessages(const QString &accountEmail, const QString &folder,
                                     const QStringList &uids);
    Q_INVOKABLE void markMessageRead(const QString &accountEmail, const QString &folder,
                                     const QString &uid);
    Q_INVOKABLE void markMessageFlagged(const QString &accountEmail, const QString &folder,
                                        const QString &uid, bool flagged);
    Q_INVOKABLE void addMessageToFolder(const QString &accountEmail, const QString &folder,
                                        const QString &uid, const QString &targetFolder);
    Q_INVOKABLE void removeMessageFromFolder(const QString &accountEmail, const QString &folder,
                                             const QString &uid, const QString &targetFolder);
    Q_INVOKABLE void copyToLocalFolder(const QString &accountEmail, const QString &folder,
                                       const QString &uid, const QString &localFolderKey);

    Q_INVOKABLE void initialize();
    void initializeConnectionPool();

    // Wire an idle watcher / background worker owned by an account.
    void wireIdleWatcher(Imap::IdleWatcher *watcher, const QString &accountEmail);
    void wireBackgroundWorker(Imap::BackgroundWorker *worker, const QString &accountEmail);

    // Per-account registry (global mode only).
    void registerAccount(const QString &email, const QVariantMap &config, Imap::ConnectionPool *pool);
    void unregisterAccount(const QString &email);

    qint32 expectedPoolConnections() const;
    qint32 poolConnectionsReady() const;
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void openAttachmentUrl(const QString &url);
    Q_INVOKABLE bool saveAttachmentUrl(const QString &url, const QString &suggestedFileName = QString());
    Q_INVOKABLE QVariantList attachmentsForMessage(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE void openAttachment(const QString &accountEmail, const QString &folderName, const QString &uid,
                                    const QString &partId, const QString &fileName, const QString &encoding);
    Q_INVOKABLE bool saveAttachment(const QString &accountEmail, const QString &folderName, const QString &uid,
                                    const QString &partId, const QString &fileName, const QString &encoding);
    Q_INVOKABLE void prefetchAttachments(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE void prefetchImageAttachments(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE QString fileSha256(const QString &localPath) const;
    Q_INVOKABLE QString dataUriSha256(const QString &dataUri) const;
    Q_INVOKABLE QString cachedAttachmentPath(const QString &accountEmail, const QString &uid, const QString &partId) const;
    Q_INVOKABLE QString attachmentPreviewPath(const QString &accountEmail, const QString &uid, const QString &partId,
                                              const QString &fileName, const QString &mimeType);

signals:
    void syncFinished(bool ok, const QString &message);
    void syncActivityChanged(bool active);
    void accountSyncActivity(const QString &accountEmail, bool active);
    void hydrateStatus(bool ok, const QString &message);
    void realtimeStatus(bool ok, const QString &message);
    void accountThrottled(const QString &accountEmail, const QString &message);
    void accountUnthrottled(const QString &accountEmail);
    void accountNeedsReauth(const QString &accountEmail);
    void attachmentReady(const QString &accountEmail, const QString &uid, const QString &partId, const QString &localPath);
    void attachmentDownloadProgress(const QString &accountEmail, const QString &uid, const QString &partId, int progressPercent);

private:
    // Internal result type for async sync work lambdas.
    struct SyncResult {
        bool ok = false;
        QString message;
        QVariantList headers;
        int inserted = 0;
    };

    DataStore         *m_store      = nullptr;
    TokenVault        *m_vault      = nullptr;

    // Per-account identity (set by the per-account constructor).
    QVariantMap m_accountConfig;
    QString m_email;
    QString m_host;
    int m_port = 993;
    Imap::AuthMethod m_authMethod = Imap::AuthMethod::XOAuth2;

    std::unique_ptr<Imap::ConnectionPool> m_pool;
    mutable QReadWriteLock                m_accountRegistryLock;
    QHash<QString, Imap::ConnectionPool*> m_accountPools;  // per-account pools (global mode)
    QHash<QString, QVariantMap>           m_accountConfigs; // per-account configs (global mode)

    std::atomic_int   m_syncInProgress { 0 };
    std::atomic_bool  m_cancelRequested { false };
    std::atomic_bool  m_destroying      { false };

    QMutex   m_pendingSyncMutex;
    QString  m_pendingFolderSync;
    bool     m_pendingFullSync  = false;
    bool     m_pendingAnnounce  = true;

    // Global m_idleWatcher kept as null for syncFolderInternal UID hint optimization.
    Imap::IdleWatcher     *m_idleWatcher = nullptr;

    QSet<QFutureWatcherBase*> m_activeWatchers;
    QMutex                    m_activeWatchersMutex;

    QSet<QString>             m_inFlightBodyHydrations;
    QMutex                    m_inFlightBodyHydrationsMutex;
    QSet<QString>             m_activeFolderSyncTargets;
    QMutex                    m_activeFolderSyncTargetsMutex;
    QHash<QString, qint64>    m_lastFolderSyncStartMs;
    QMutex                    m_lastFolderSyncStartMsMutex;
    QSet<QString>             m_backfilledFolders;
    QMutex                    m_backfilledFoldersMutex;
    QSet<QString>             m_activeBgHydrateFolders;
    QSet<QString>             m_pendingBgHydrateFolders;
    QMutex                    m_bgHydrateMutex;

    QHash<QString, bool>      m_accountThrottleState;
    QMutex                    m_throttleStateMutex;
    bool                       m_offlineMode = false;
    qint32                     m_expectedPoolSize = 0;

    struct AttachmentCacheEntry { QString localPath; qint64 expiresAt = 0; };
    mutable QHash<QString, AttachmentCacheEntry> m_attachmentFileCache;
    mutable QMutex                               m_attachmentFileCacheMutex;


    QSet<QString>                                m_inFlightAttachmentDownloads;
    mutable QMutex                               m_inFlightAttachmentDownloadsMutex;

    struct AccountInfo { QString email, host, accessToken; int port = 0; QString authType; };
    struct SyncFolderOptions {
        bool announce = true;
    };
    [[nodiscard]] bool isPerAccountMode() const { return !m_email.isEmpty(); }
    void installThrottleObserver();
    [[nodiscard]] Imap::ConnectionPool* poolForEmail(const QString &email) const;
    QList<AccountInfo> resolveAccounts(const QVariantList &accounts);

public:
    QStringList syncTargetsForAccount(const QString &email, const QString &host) const;

    // Exposed for GoogleApiService wiring.
    [[nodiscard]] QVariantList accountConfigList() const;
    QString refreshAccessToken(const QVariantMap &account, const QString &email);

private:

    // Global workers removed — accounts own their own via IAccount::initialize.
    // wireIdleWatcher/wireBackgroundWorker handle signal setup.
    void workerEmitRealtimeStatus(bool ok, const QString &message);
    void backgroundFetchBodies(const QString &email, const QString &folder);
    void hydrateFolderBodies(const QString &email, const QString &folder,
                             const QString &key, qint32 limit);
    void saveFolderStatusSnapshot(const QString &accountEmail, const QString &folder,
                                  qint64 uidNext, qint64 highestModSeq, qint64 messages);
    void registerWatcher(QFutureWatcherBase *watcher);
    void unregisterWatcher(QFutureWatcherBase *watcher);
    void waitForActiveWatchers(int timeoutMs);
    void drainPendingSync();
    void runAsync(std::function<SyncResult()> work, std::function<void(const SyncResult&)> onDone);
    void runBackgroundTask(std::function<void()> task);
    void attachmentCacheInsert(const QString &key, const QString &localPath);

    QString workerRefreshAccessToken(const QVariantMap &account, const QString &email);

    void backgroundOnIdleLiveUpdate(const QString &email);

    [[nodiscard]] QVariantList workerGetAccounts() const;
    [[nodiscard]] QVariantMap loadFolderStatusSnapshot(const QString &accountEmail, const QString &folder) const;

    QVariantList fetchFolderHeaders(const QString &email,
                                   QString *statusOut,
                                   const QString &folderName = QStringLiteral("INBOX"),
                                   const std::function<void(const QVariantMap &)> &onHeader = {},
                                   qint64 minUidExclusive = -1,
                                   bool reconcileDeletes = false,
                                   qint32 fetchBudget = -1);

    QVariantList syncFolderInternal(const AccountInfo &account,
                                    const QString &folder,
                                    const SyncFolderOptions &options,
                                    qint32 &seqNum,
                                    qint32 &inboxInserted,
                                    QVariantList &pendingHeaders,
                                    QVariantList &resultHeaders,
                                    QElapsedTimer &flushTimer,
                                    const std::function<void()> &flush);

    std::function<void()> makeSyncFlushLambda(QVariantList &pendingHeaders,
                                              QElapsedTimer &flushTimer);

    void prefetchAttachmentsInternal(const QString &accountEmail,
                                     const QString &folderName,
                                     const QString &uid,
                                     const bool imagesOnly);

    void hydrateMessageBodyInternal(const QString &accountEmail,
                                    const QString &folderName,
                                    const QString &uid,
                                    bool userInitiated);

    QString executeHydration(const QVariantMap &account,
                             const QString &email,
                             const QString &folder,
                             const QString &uid,
                             bool userInitiated);
};
