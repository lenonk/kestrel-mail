#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <QFuture>
#include <QHash>
#include <QSet>
#include <QVariantList>

#include "sync/idlewatcher.h"
#include "sync/backgroundworker.h"

namespace Imap { class Connection; }

class AccountRepository;
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
    explicit ImapService(AccountRepository *accounts, DataStore *store, TokenVault *vault, QObject *parent = nullptr);
    ~ImapService() override;

    Q_INVOKABLE void syncAll(bool announce = true);
    static std::shared_ptr<Imap::Connection> getPooledConnection(const QString &email = {}, const QString &owner = {});
    Q_INVOKABLE void syncFolder(const QString &folderName, bool announce = true);
    Q_INVOKABLE void refreshFolderList(bool announce = true);
    Q_INVOKABLE void hydrateMessageBody(const QString &accountEmail, const QString &folderName, const QString &uid);
    Q_INVOKABLE void moveMessage(const QString &accountEmail, const QString &folder,
                                 const QString &uid, const QString &targetFolder);
    Q_INVOKABLE void markMessageRead(const QString &accountEmail, const QString &folder,
                                     const QString &uid);

    Q_INVOKABLE void initialize();
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
    void hydrateStatus(bool ok, const QString &message);
    void realtimeStatus(bool ok, const QString &message);
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

    AccountRepository *m_accounts   = nullptr;
    DataStore         *m_store      = nullptr;
    TokenVault        *m_vault      = nullptr;

    std::atomic_int   m_syncInProgress { 0 };
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

    QSet<QString>             m_inFlightBodyHydrations;
    QMutex                    m_inFlightBodyHydrationsMutex;
    QSet<QString>             m_activeFolderSyncTargets;
    QMutex                    m_activeFolderSyncTargetsMutex;
    QSet<QString>             m_activeBgHydrateFolders;
    QSet<QString>             m_pendingBgHydrateFolders;
    QMutex                    m_bgHydrateMutex;

    struct AttachmentCacheEntry { QString localPath; qint64 expiresAt = 0; };
    mutable QHash<QString, AttachmentCacheEntry> m_attachmentFileCache;
    mutable QMutex                               m_attachmentFileCacheMutex;

    QSet<QString>                                m_inFlightAttachmentDownloads;
    mutable QMutex                               m_inFlightAttachmentDownloadsMutex;

    struct AccountInfo { QString email, host, accessToken; int port = 0; };
    struct SyncFolderOptions {
        bool announce = true;
    };
    QList<AccountInfo> resolveAccounts(const QVariantList &accounts);

    void startIdleWatcher();
    void stopIdleWatcher(bool waitForStop = true) const;
    void startBackgroundWorker();
    void stopBackgroundWorker(bool waitForStop = true) const;
    void idlePruneFolderToUids(const QString &email, const QString &folder, const QStringList &uids);
    void idleRemoveUids(const QString &email, const QStringList &uids);
    void idleOnInboxChanged();
    void workerEmitRealtimeStatus(bool ok, const QString &message);
    void backgroundSyncHeadersAndFlags(const QVariantMap &account, const QString &email,
                                       const QString &folder, const QString &accessToken);
    void backgroundFetchBodies(const QVariantMap &account, const QString &email,
                               const QString &folder, const QString &accessToken);
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
    QString refreshAccessToken(const QVariantMap &account, const QString &email);
    QStringList idleGetFolderUids(const QString &email, const QString &folder);

    void backgroundOnIdleLiveUpdate(const QVariantMap &account, const QString &email);

    [[nodiscard]] QVariantList workerGetAccounts() const;
    [[nodiscard]] QVariantMap loadFolderStatusSnapshot(const QString &accountEmail, const QString &folder) const;

    QVariantList fetchFolderHeaders(const QString &email,
                                   QString *statusOut,
                                   const QString &folderName = QStringLiteral("INBOX"),
                                   const std::function<void(const QVariantMap &)> &onHeader = {},
                                   qint64 minUidExclusive = -1,
                                   bool reconcileDeletes = false,
                                   int fetchBudget = -1);

    QVariantList syncFolderInternal(const AccountInfo &account,
                                    const QString &folder,
                                    const SyncFolderOptions &options,
                                    int &seqNum,
                                    int &inboxInserted,
                                    QVariantList &pendingHeaders,
                                    QVariantList &resultHeaders,
                                    QElapsedTimer &flushTimer,
                                    const std::function<void()> &flush);

    void hydrateMessageBodyInternal(const QString &accountEmail,
                                    const QString &folderName,
                                    const QString &uid,
                                    bool userInitiated);
};
