#include "imapservice.h"

#include "../../accounts/accountrepository.h"
#include "../../auth/tokenvault.h"
#include "../../store/datastore.h"
#include "connection/imapconnection.h"
#include "sync/syncutils.h"
#include "sync/syncengine.h"
#include "message/messagehydrator.h"
#include "message/bodyprocessor.h"
#include "parser/responseparser.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QMimeDatabase>
#include <QtConcurrentRun>

#include "sync/idlewatcher.h"

using namespace Qt::Literals::StringLiterals;

ImapService::ImapService(AccountRepository *accounts, DataStore *store, TokenVault *vault, QObject *parent)
    : QObject(parent)
    , m_accounts(accounts)
    , m_store(store)
    , m_vault(vault) { }

ImapService::~ImapService() { shutdown(); }

void
ImapService::registerWatcher(QFutureWatcherBase *watcher) {
    if (!watcher)
        return;

    QMutexLocker locker(&m_activeWatchersMutex);
    m_activeWatchers.insert(watcher);
}

void
ImapService::unregisterWatcher(QFutureWatcherBase *watcher) {
    if (!watcher) {
        return;
    }

    QMutexLocker locker(&m_activeWatchersMutex);
    m_activeWatchers.remove(watcher);
}

void
ImapService::waitForActiveWatchers(const qint32 timeoutMs) {
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QList<QFutureWatcherBase*> snapshot;

        {
            QMutexLocker locker(&m_activeWatchersMutex);
            snapshot = m_activeWatchers.values();
        }

        if (snapshot.isEmpty()) return;

        bool anyRunning = false;
        for (const QFutureWatcherBase *watcher : snapshot) {
            if (!watcher)
                continue;

            if (watcher->isRunning()) {
                anyRunning = true;
            }
        }

        if (!anyRunning)
            return;

        QThread::msleep(20);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    }
}

void
ImapService::shutdown() {
    if (m_destroying.exchange(true)) return;

    m_cancelRequested.store(true);

    {
        QMutexLocker locker(&m_pendingSyncMutex);
        m_pendingFullSync = false;
        m_pendingFolderSync.clear();
        m_pendingAnnounce = false;
    }

    // Graceful app-exit path: stop worker threads and wait so QThread objects are
    // not destroyed while still running.
    stopIdleWatcher(true);
    stopBackgroundWorker(true);

    // Allow in-flight async watchers to settle before object teardown.
    waitForActiveWatchers(2000);
}

void
ImapService::openAttachmentUrl(const QString &url) {
    const QUrl qurl = QUrl::fromUserInput(url.trimmed());
    if (!qurl.isValid() || qurl.isEmpty())
        return;
    QDesktopServices::openUrl(qurl);
}

bool
ImapService::saveAttachmentUrl(const QString &url, const QString &suggestedFileName) {
    const QUrl qurl = QUrl::fromUserInput(url.trimmed());
    if (!qurl.isValid() || qurl.isEmpty())
        return false;

    QString defaultName = suggestedFileName.trimmed();
    if (defaultName.isEmpty())
        defaultName = QFileInfo(qurl.path()).fileName();
    if (defaultName.isEmpty())
        defaultName = QStringLiteral("attachment");

    const QString outPath = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save Attachment"), defaultName);
    if (outPath.isEmpty())
        return false;

    QNetworkAccessManager nam;
    QNetworkRequest req(qurl);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray data = reply->readAll();
    const bool okReply = (reply->error() == QNetworkReply::NoError) && !data.isEmpty();
    reply->deleteLater();
    if (!okReply)
        return false;

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const auto written = f.write(data);
    f.close();
    return written == data.size();
}

QVariantList
ImapService::attachmentsForMessage(const QString &accountEmail, const QString &folderName, const QString &uid) {
    if (!m_store) return {};
    return m_store->attachmentsForMessage(accountEmail, folderName, uid);
}

static constexpr qint64 kAttachmentCacheTtlMs = 60LL * 60 * 1000; // 60 minutes

static QString attachmentCacheKey(const QString &email, const QString &uid, const QString &partId) {
    return email + "|"_L1 + uid + "|"_L1 + partId;
}

QString
ImapService::cachedAttachmentPath(const QString &accountEmail, const QString &uid, const QString &partId) const {
    QMutexLocker locker(&m_attachmentFileCacheMutex);
    auto it = m_attachmentFileCache.find(attachmentCacheKey(accountEmail, uid, partId));
    if (it == m_attachmentFileCache.end())
        return {};
    if (QDateTime::currentMSecsSinceEpoch() > it->expiresAt) {
        QFile::remove(it->localPath);
        m_attachmentFileCache.erase(it);
        return {};
    }
    return it->localPath;
}

void
ImapService::attachmentCacheInsert(const QString &key, const QString &localPath) {
    QMutexLocker locker(&m_attachmentFileCacheMutex);
    if (m_attachmentFileCache.contains(key))
        return;
    // Evict expired entries before inserting.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_attachmentFileCache.begin(); it != m_attachmentFileCache.end(); ) {
        if (now > it->expiresAt) {
            QFile::remove(it->localPath);
            it = m_attachmentFileCache.erase(it);
        } else {
            ++it;
        }
    }
    m_attachmentFileCache.insert(key, { localPath, now + kAttachmentCacheTtlMs });
}

void
ImapService::runBackgroundTask(std::function<void()> task) {
    auto *watcher = new QFutureWatcher<void>(this);
    registerWatcher(watcher);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        unregisterWatcher(watcher);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::move(task)));
}

// Forward declaration — defined later in this file.
static QByteArray fetchAttachmentPartById(Imap::Connection &cxn, const QString &uid,
                                          const QString &partId, const QString &encoding,
                                          const std::function<void(int, qint64)> &onProgress = {});

void
ImapService::prefetchAttachments(const QString &accountEmail, const QString &folderName, const QString &uid) {
    if (!m_store || !m_accounts)
        return;

    const QVariantList attachments = m_store->attachmentsForMessage(accountEmail, folderName, uid);
    if (attachments.isEmpty())
        return;

    // Resolve account on the main thread before going async.
    QVariantMap account;
    for (const auto &a : m_accounts->accounts()) {
        const auto row = a.toMap();
        if (row.value("email"_L1).toString().compare(accountEmail, Qt::CaseInsensitive) == 0) {
            account = row;
            break;
        }
    }
    if (account.isEmpty())
        return;

    // Filter out already-cached (and not-yet-expired) attachments.
    QVariantList toFetch;
    for (const auto &a : attachments) {
        const auto am = a.toMap();
        if (cachedAttachmentPath(accountEmail, uid, am.value("partId"_L1).toString()).isEmpty())
            toFetch.push_back(a);
    }
    if (toFetch.isEmpty())
        return;

    const QString host = account.value("imapHost"_L1).toString();
    const int     port = account.value("imapPort"_L1).toInt();

    runBackgroundTask([this, host, port, accountEmail, folderName, uid, account, toFetch]() {
        const QString token = workerRefreshAccessToken(account, accountEmail);
        if (token.isEmpty())
            return;

        Imap::Connection cxn;
        if (!cxn.connectAndAuth(host, port, accountEmail, token).success)
            return;
        const auto [selectOk, _] = cxn.select(folderName);
        if (!selectOk)
            return;

        const QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                + "/attachments/"_L1 + accountEmail + "/"_L1 + uid;

        for (const auto &a : toFetch) {
            if (m_destroying.load())
                break;

            const auto    am       = a.toMap();
            const QString partId   = am.value("partId"_L1).toString();
            const QString encoding = am.value("encoding"_L1).toString();
            const QString name     = am.value("name"_L1).toString();

            const QString key = attachmentCacheKey(accountEmail, uid, partId);
            if (!cachedAttachmentPath(accountEmail, uid, partId).isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
                continue; // already cached by a concurrent call
            }

            const QByteArray data = fetchAttachmentPartById(cxn, uid, partId, encoding,
                [this, &accountEmail, &uid, &partId](const int percent, const qint64) {
                    emit attachmentDownloadProgress(accountEmail, uid, partId, percent);
                });
            if (data.isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
                continue;
            }

            // Use a per-part subdirectory so the file has a clean user-visible name.
            const QString safePartId = QString(partId).replace('/', '_').replace('.', '_');
            const QString partDir    = cacheBase + "/"_L1 + safePartId;
            QDir().mkpath(partDir);

            const QString safeName  = QString(name).replace(QRegularExpression("[^A-Za-z0-9._-]"_L1), "_"_L1);
            const QString localPath = partDir + "/"_L1 + (safeName.isEmpty() ? "attachment"_L1 : safeName);

            QSaveFile f(localPath);
            if (!f.open(QIODevice::WriteOnly))
                continue;
            f.write(data);
            if (!f.commit())
                continue;

            attachmentCacheInsert(key, localPath);
            emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
            emit attachmentReady(accountEmail, uid, partId, localPath);
        }
    });
}

static QByteArray
fetchAttachmentPartById(Imap::Connection &cxn, const QString &uid, const QString &partId, const QString &encoding,
                        const std::function<void(int, qint64)> &onProgress) {
    QString status;
    QByteArray literal = cxn.fetchMimePartWithProgress(uid, partId, 5, onProgress, &status);
    if (literal.isEmpty())
        return {};

    const QString enc = encoding.trimmed().toLower();
    if (enc == "base64"_L1) {
        literal.replace("\r", "");
        literal.replace("\n", "");
        return QByteArray::fromBase64(literal);
    }
    if (enc == "quoted-printable"_L1)
        return Imap::BodyProcessor::decodeQuotedPrintable(literal);
    return literal;
}

void
ImapService::openAttachment(const QString &accountEmail, const QString &, const QString &uid,
                            const QString &partId, const QString &, const QString &) {
    const QString cached = cachedAttachmentPath(accountEmail, uid, partId);
    if (!cached.isEmpty() && QFile::exists(cached)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(cached));
        return;
    }

    emit realtimeStatus(false, QStringLiteral("Attachment is still downloading. Please try again shortly."));
}

bool
ImapService::saveAttachment(const QString &accountEmail, const QString &, const QString &uid,
                            const QString &partId, const QString &fileName, const QString &) {
    const QString cached = cachedAttachmentPath(accountEmail, uid, partId);
    if (cached.isEmpty() || !QFile::exists(cached)) {
        emit realtimeStatus(false, QStringLiteral("Attachment is still downloading. Please try again shortly."));
        return false;
    }

    QFile in(cached);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = in.readAll();
    in.close();

    if (data.isEmpty())
        return false;

    const QString outPath = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save Attachment"),
                                                         fileName.isEmpty() ? QStringLiteral("attachment") : fileName);
    if (outPath.isEmpty())
        return false;

    QSaveFile f(outPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

void
ImapService::drainPendingSync() {
    if (m_destroying.load()) return;
    QString pending;
    bool pendingFull    = false;
    bool pendingAnnounce;

    {
        QMutexLocker l(&m_pendingSyncMutex);
        pendingFull     = m_pendingFullSync;  m_pendingFullSync  = false;
        pending         = m_pendingFolderSync; m_pendingFolderSync.clear();
        pendingAnnounce = m_pendingAnnounce;  m_pendingAnnounce  = true;
    }

    if (pendingFull)
        QMetaObject::invokeMethod(this, [this, pendingAnnounce]() { syncAll(pendingAnnounce); },
                                  Qt::QueuedConnection);
    else if (!pending.isEmpty())
        QMetaObject::invokeMethod(this, [this, pending, pendingAnnounce]() { syncFolder(pending, pendingAnnounce); },
                                  Qt::QueuedConnection);
}

void
ImapService::runAsync(std::function<SyncResult()> work, std::function<void(const SyncResult&)> onDone) {
    m_cancelRequested.store(false);
    m_syncInProgress = true;
    emit syncActivityChanged(true);

    auto *watcher = new QFutureWatcher<SyncResult>(this);
    registerWatcher(watcher);
    connect(watcher, &QFutureWatcher<SyncResult>::finished, this,
            [this, watcher, onDone = std::move(onDone)]() {
        const auto r = watcher->result();

        if (!m_destroying && onDone)
            onDone(r);

        m_syncInProgress = false;
        emit syncActivityChanged(false);

        unregisterWatcher(watcher);
        watcher->deleteLater();

        drainPendingSync();
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

void
ImapService::initialize() {
    startBackgroundWorker();
    startIdleWatcher();
}

void
ImapService::startIdleWatcher() {
    if (m_destroying || m_idleWatcher)
        return;

    m_idleThread = new QThread(this);
    m_idleWatcher = new Imap::IdleWatcher();
    m_idleWatcher->moveToThread(m_idleThread);

    connect(m_idleThread, &QThread::started, m_idleWatcher, &Imap::IdleWatcher::start);

    connect(m_idleWatcher, &Imap::IdleWatcher::requestAccounts,
            this, [this](QVariantList *out) {
                if (out) *out = workerGetAccounts();
            }, Qt::BlockingQueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::requestRefreshAccessToken,
            this, [this](const QVariantMap &account, const QString &email, QString *out) {
                if (out) *out = workerRefreshAccessToken(account, email);
            }, Qt::BlockingQueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::requestFolderUids,
            this, [this](const QString &email, const QString &folder, QStringList *out) {
                if (out) *out = idleGetFolderUids(email, folder);
            }, Qt::BlockingQueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::pruneFolderToUidsRequested,
            this, [this](const QString &email, const QString &folder, const QStringList &uids) {
                idlePruneFolderToUids(email, folder, uids);
            }, Qt::QueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::removeUidsRequested,
            this, [this](const QString &email, const QStringList &uids) {
                idleRemoveUids(email, uids);
            }, Qt::QueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::inboxChanged,
            this, [this]() { idleOnInboxChanged(); }, Qt::QueuedConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::realtimeStatus,
            this, [this](const bool ok, const QString &message) {
                workerEmitRealtimeStatus(ok, message);
            }, Qt::QueuedConnection);

    connect(m_idleThread, &QThread::finished, m_idleWatcher, &QObject::deleteLater);
    connect(m_idleThread, &QThread::finished, this, [this]() {
        m_idleWatcher = nullptr;
        if (m_idleThread) {
            m_idleThread->deleteLater();
            m_idleThread = nullptr;
        }
    });

    m_idleThread->start();
}

void
ImapService::stopIdleWatcher(const bool waitForStop) const {
    if (m_syncTimer)
        m_syncTimer->stop();

    if (!m_idleWatcher || !m_idleThread)
        return;

    QMetaObject::invokeMethod(m_idleWatcher, "stop", Qt::QueuedConnection);
    m_idleThread->quit();

    if (waitForStop)
        m_idleThread->wait();
}

void
ImapService::startBackgroundWorker() {
    if (m_destroying || m_backgroundWorker)
        return;

    m_backgroundThread = new QThread(this);
    m_backgroundWorker = new Imap::BackgroundWorker();
    m_backgroundWorker->setIntervalSeconds(120);
    m_backgroundWorker->moveToThread(m_backgroundThread);

    connect(m_backgroundThread, &QThread::started,
            m_backgroundWorker, &Imap::BackgroundWorker::start);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::requestAccounts,
            this, [this](QVariantList *out) {
                if (out) *out = workerGetAccounts();
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::requestRefreshAccessToken,
            this, [this](const QVariantMap &account, const QString &email, QString *out) {
                if (out) *out = workerRefreshAccessToken(account, email);
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::loginSessionStartupRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &accessToken) {
                backgroundLoginSessionStartup(account, email, accessToken);
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::listFoldersRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &accessToken, QStringList *out) {
                if (out) *out = backgroundListFolders(account, email, accessToken);
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::loadFolderStatusSnapshotRequested,
            this, [this](const QString &accountEmail, const QString &folder,
                         qint64 *uidNext, qint64 *highestModSeq, qint64 *messages, bool *found) {
                const auto row = loadFolderStatusSnapshot(accountEmail, folder);
                const bool exists = !row.isEmpty();
                if (found) *found = exists;
                if (!exists)
                    return;
                if (uidNext) *uidNext = row.value("uidNext"_L1).toLongLong();
                if (highestModSeq) *highestModSeq = row.value("highestModSeq"_L1).toLongLong();
                if (messages) *messages = row.value("messages"_L1).toLongLong();
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::saveFolderStatusSnapshotRequested,
            this, [this](const QString &accountEmail, const QString &folder,
                         const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) {
                saveFolderStatusSnapshot(accountEmail, folder, uidNext, highestModSeq, messages);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::shouldSyncFolderRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &folder,
                         const QString &accessToken, bool *out) {
                if (!out)
                    return;
                *out = (*out) && backgroundShouldSyncFolder(account, email, folder, accessToken);
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::syncHeadersAndFlagsRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &folder,
                         const QString &accessToken) {
                backgroundSyncHeadersAndFlags(account, email, folder, accessToken);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::fetchBodiesRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &folder,
                         const QString &accessToken) {
                backgroundFetchBodies(account, email, folder, accessToken);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::idleLiveUpdateRequested,
            this, [this](const QVariantMap &account, const QString &email) {
                backgroundOnIdleLiveUpdate(account, email);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::loopError,
            this, [this](const QString &message) {
                backgroundOnLoopError(message);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::realtimeStatus,
            this, [this](const bool ok, const QString &message) {
                workerEmitRealtimeStatus(ok, message);
            }, Qt::QueuedConnection);

    connect(m_backgroundThread, &QThread::finished, m_backgroundWorker, &QObject::deleteLater);
    connect(m_backgroundThread, &QThread::finished, this, [this]() {
        m_backgroundWorker = nullptr;
        if (m_backgroundThread) {
            m_backgroundThread->deleteLater();
            m_backgroundThread = nullptr;
        }
    });

    m_backgroundThread->start();
}

void
ImapService::stopBackgroundWorker(const bool waitForStop) const {
    if (!m_backgroundWorker || !m_backgroundThread)
        return;

    QMetaObject::invokeMethod(m_backgroundWorker, "stop", Qt::QueuedConnection);
    m_backgroundThread->quit();

    if (waitForStop)
        m_backgroundThread->wait();
}

QVariantList
ImapService::workerGetAccounts() const {
    return m_accounts ? m_accounts->accounts() : QVariantList{};
}

QString
ImapService::workerRefreshAccessToken(const QVariantMap &account, const QString &email) {
    return refreshAccessToken(account, email);
}

QStringList
ImapService::idleGetFolderUids(const QString &email, const QString &folder) {
    QStringList result;

    auto readFn = [this, &result, email, folder]() {
        if (m_store)
            result = m_store->folderUids(email, folder);
    };

    if (QThread::currentThread() == thread()) {
        readFn();
    } else {
        QMetaObject::invokeMethod(this, readFn, Qt::BlockingQueuedConnection);
    }

    return result;
}

void
ImapService::idlePruneFolderToUids(const QString &email, const QString &folder, const QStringList &uids) {
    auto fn = [this, email, folder, uids]() {
        if (m_store)
            m_store->pruneFolderToUids(email, folder, uids);
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

void
ImapService::idleRemoveUids(const QString &email, const QStringList &uids) {
    auto fn = [this, email, uids]() {
        if (m_store)
            m_store->removeAccountUidsEverywhere(email, uids);
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

void
ImapService::idleOnInboxChanged() {
    auto fn = [this]() { syncFolder("INBOX"_L1, false); };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

void
ImapService::workerEmitRealtimeStatus(const bool ok, const QString &message) {
    auto fn = [this, ok, message]() { emit realtimeStatus(ok, message); };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

void
ImapService::backgroundOnLoopError(const QString &message) {
    workerEmitRealtimeStatus(false, message);
}

void
ImapService::backgroundLoginSessionStartup(const QVariantMap &account, const QString &email,
                                           const QString &accessToken) {
    const QString host = account.value("imapHost"_L1).toString();
    const int port = account.value("imapPort"_L1).toInt();

    QString status;
    const QVariantList folders = Imap::SyncEngine::fetchFolders(host, port, email, accessToken, &status);

    for (const QVariant &fv : folders) {
        const QVariantMap folder = fv.toMap();
        if (m_store)
            m_store->upsertFolder(folder);
    }

    if (!status.trimmed().isEmpty()) {
        const bool statusOk = !status.contains("failed"_L1, Qt::CaseInsensitive)
                           && !status.contains("error"_L1, Qt::CaseInsensitive);
        workerEmitRealtimeStatus(statusOk, status.trimmed());
    }
}

QStringList
ImapService::backgroundListFolders(const QVariantMap &account, const QString &email, const QString &accessToken) {
    QString status;
    const QString host = account.value("imapHost"_L1).toString();
    const int port = account.value("imapPort"_L1).toInt();
    const QVariantList folders = Imap::SyncEngine::fetchFolders(host, port, email, accessToken, &status);

    QSet<QString> parentContainers;
    for (const QVariant &f : folders) {
        const QString name = f.toMap().value("name"_L1).toString().trimmed();
        const int slash = name.indexOf('/');
        if (slash > 0)
            parentContainers.insert(name.left(slash).toLower());
    }

    QStringList out;
    out.reserve(folders.size());
    for (const QVariant &f : folders) {
        const auto row = f.toMap();
        const QString name = row.value("name"_L1).toString().trimmed();
        const QString flags = row.value("flags"_L1).toString().toLower();
        const QString lowerName = name.toLower();

        const bool isNoSelect = flags.contains("\\noselect"_L1);
        const bool isCategory = name.contains("/Categories/"_L1, Qt::CaseInsensitive);
        const bool isContainerRoot = name.compare("[Gmail]"_L1, Qt::CaseInsensitive) == 0
                                  || name.compare("[Google Mail]"_L1, Qt::CaseInsensitive) == 0;
        const bool isParentContainer = parentContainers.contains(lowerName);

        if (name.isEmpty() || isNoSelect || isCategory || isContainerRoot || isParentContainer)
            continue;

        out.push_back(name);
    }

    return out;
}

bool
ImapService::backgroundShouldSyncFolder(const QVariantMap &, const QString &, const QString &folder,
                                        const QString &) {
    const auto name = folder.trimmed();
    if (name.isEmpty())
        return false;

    // Skip synthetic/container folders that are not message-bearing.
    if (name.startsWith('[') && name.endsWith(']'))
        return false;

    return true;
}

void
ImapService::backgroundSyncHeadersAndFlags(const QVariantMap &, const QString &, const QString &folder,
                                           const QString &) {
    QMetaObject::invokeMethod(this, [this, folder]() {
        syncFolder(folder, false);
    }, Qt::QueuedConnection);
}

void
ImapService::backgroundFetchBodies(const QVariantMap &, const QString &email, const QString &folder,
                                   const QString &) {
    if (!m_store)
        return;

    bool ok = false;
    const int configuredLimit = qEnvironmentVariableIntValue("KESTREL_BG_BODY_FETCH_LIMIT", &ok);
    const int limit = ok && configuredLimit > 0 ? configuredLimit : 8;

    QStringList candidates;
    if (QThread::currentThread() == thread()) {
        candidates = m_store->bodyFetchCandidates(email, folder, limit);
    } else {
        QMetaObject::invokeMethod(this, [this, &candidates, email, folder, limit]() {
            if (m_store)
                candidates = m_store->bodyFetchCandidates(email, folder, limit);
        }, Qt::BlockingQueuedConnection);
    }

    for (const auto &uid : candidates)
        hydrateMessageBodyInternal(email, folder, uid, false);
}

void
ImapService::backgroundOnIdleLiveUpdate(const QVariantMap &, const QString &) {
    auto fn = [this]() {
        if (m_destroying)
            return;

        if (!m_idleWatcher || !m_idleWatcher->isRunning()) {
            startIdleWatcher();
            emit realtimeStatus(true, QStringLiteral("Idle/live watcher (re)started."));
        }
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

QVariantMap
ImapService::loadFolderStatusSnapshot(const QString &accountEmail, const QString &folder) const {
    if (!m_store)
        return {};

    if (QThread::currentThread() == thread())
        return m_store->folderSyncStatus(accountEmail, folder);

    QVariantMap out;
    QMetaObject::invokeMethod(const_cast<ImapService*>(this), [this, &out, accountEmail, folder]() {
        if (m_store)
            out = m_store->folderSyncStatus(accountEmail, folder);
    }, Qt::BlockingQueuedConnection);
    return out;
}

void
ImapService::saveFolderStatusSnapshot(const QString &accountEmail, const QString &folder,
                                      const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) {
    if (!m_store)
        return;

    auto fn = [this, accountEmail, folder, uidNext, highestModSeq, messages]() {
        if (m_store)
            m_store->upsertFolderSyncStatus(accountEmail, folder, uidNext, highestModSeq, messages);
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

QString
ImapService::refreshAccessToken(const QVariantMap &account, const QString &email) {
    if (!m_vault)
        return {};

    const auto refreshToken = m_vault->loadRefreshToken(email);
    if (refreshToken.isEmpty())
        return {};

    const auto tokenUrl = account.value("oauthTokenUrl"_L1).toString();

    auto clientId = account.value("oauthClientId"_L1).toString().trimmed();
    auto clientSecret = account.value("oauthClientSecret"_L1).toString();

    if (clientId.isEmpty() && account.value("providerId"_L1).toString() == "gmail"_L1) {
        clientId = QString("");
        if (clientSecret.isEmpty()) {
            clientSecret = QString("");
        }
    }

    if (tokenUrl.isEmpty() || clientId.isEmpty()) return {};

    QNetworkAccessManager nam;
    QNetworkRequest req { QUrl(tokenUrl) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded"_L1);

    QUrlQuery body;
    body.addQueryItem("grant_type"_L1, "refresh_token"_L1);
    body.addQueryItem("refresh_token"_L1, refreshToken);
    body.addQueryItem("client_id"_L1, clientId);

    if (!clientSecret.isEmpty()) {
        body.addQueryItem("client_secret"_L1, clientSecret);
    }

    auto *reply = nam.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto payload = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    if (!ok)
        return {};

    const auto obj = QJsonDocument::fromJson(payload).object();
    return obj.value("access_token"_L1).toString();
}

QList<ImapService::AccountInfo>
ImapService::resolveAccounts(const QVariantList &accounts) {
    QList<AccountInfo> result;
    for (const auto &a : accounts) {
        const auto acc = a.toMap();

        if (acc.value("authType"_L1).toString() != "oauth2"_L1)
            continue;

        const auto email = acc.value("email"_L1).toString();
        const auto host  = acc.value("imapHost"_L1).toString();
        const int  port  = acc.value("imapPort"_L1).toInt();

        if (email.isEmpty() || host.isEmpty() || port <= 0)
            continue;

        const auto token = refreshAccessToken(acc, email);

        if (token.isEmpty())
            continue;

        result.push_back({email, host, token, port});
    }

    return result;
}

QVariantList
ImapService::fetchFolderHeaders(const QString &host, int port, const QString &email,
                                const QString &accessToken, QString *statusOut,
                                const QString &folderName,
                                const std::function<void(const QVariantMap &)> &onHeader,
                                qint64 minUidExclusive,
                                bool reconcileDeletes,
                                int fetchBudget) {
    if (accessToken.isEmpty()) {
        if (statusOut)
            *statusOut = "No access token for fetch."_L1;
        return {};
    }

    auto cxn = std::make_shared<Imap::Connection>();
    if (const auto result = cxn->connectAndAuth(host, port, email, accessToken); !result.success) {
        if (statusOut) *statusOut = result.message;
        return {};
    }

    // ── Build context and delegate to SyncEngine ─────────────────────────────
    Imap::SyncContext ctx;
    ctx.cxn                     = cxn;
    ctx.folderName              = folderName;
    ctx.minUidExclusive         = minUidExclusive;
    ctx.reconcileDeletes        = reconcileDeletes;
    ctx.fetchBudget             = fetchBudget;
    ctx.cancelRequested         = &m_cancelRequested;
    ctx.onHeader                = onHeader;

    ctx.avatarShouldRefresh = [this](const QString &senderEmail, int ttlSecs, int maxFailures) -> bool {
        bool result = true;
        QMetaObject::invokeMethod(this, [this, &result, senderEmail, ttlSecs, maxFailures]() {
            if (m_store) result = m_store->avatarShouldRefresh(senderEmail, ttlSecs, maxFailures);
        }, Qt::BlockingQueuedConnection);
        return result;
    };

    ctx.getFolderUids = [this](const QString &acctEmail, const QString &folder) -> QStringList {
        QStringList result;
        QMetaObject::invokeMethod(this, [this, &result, acctEmail, folder]() {
            if (m_store) result = m_store->folderUids(acctEmail, folder);
        }, Qt::BlockingQueuedConnection);
        return result;
    };

    ctx.removeUids = [this](const QString &acctEmail, const QStringList &uids) {
        QMetaObject::invokeMethod(this, [this, acctEmail, uids]() {
            if (m_store) m_store->removeAccountUidsEverywhere(acctEmail, uids);
        }, Qt::QueuedConnection);
    };

    ctx.onFlagsReconciled = [this](const QString &acctEmail, const QString &folder,
                                    const QStringList &readUids) {
        QMetaObject::invokeMethod(this, [this, acctEmail, folder, readUids]() {
            if (m_store) m_store->reconcileReadFlags(acctEmail, folder, readUids);
        }, Qt::QueuedConnection);
    };

    // SyncEngine::execute() handles SELECT internally.
    const Imap::SyncResult syncResult = Imap::SyncEngine{}.execute(ctx);

    if (statusOut)
        *statusOut = syncResult.statusMessage;

    return syncResult.headers;
}

QVariantList
ImapService::syncFolderInternal(const AccountInfo &account,
                                  const QString &target,
                                  const SyncFolderOptions &options,
                                  int &seqNum,
                                  int &inboxInserted,
                                  QVariantList &pendingHeaders,
                                  QVariantList &resultHeaders,
                                  QElapsedTimer &flushTimer,
                                  const std::function<void()> &flush) {
    const bool isInbox = (target.compare("INBOX"_L1, Qt::CaseInsensitive) == 0);
    const QString &email = account.email;
    const QString &host = account.host;
    const QString &accessToken = account.accessToken;
    const int port = account.port;
    const bool announce = options.announce;

    qint64 dbMaxUid = 0;
    QMetaObject::invokeMethod(this, [this, &dbMaxUid, email = email, &target]() {
        if (m_store) dbMaxUid = m_store->folderMaxUid(email, target);
    }, Qt::BlockingQueuedConnection);

    qint64 minUid = dbMaxUid;
    bool hadIdleHint = false;

    if (isInbox && !announce && m_idleWatcher) {
        const qint64 idleHint = m_idleWatcher->minUidHint.exchange(0);
        hadIdleHint = (idleHint > 0);
        const qint64 watermark = m_idleWatcher->maxUidWatermark.load();
        minUid = qMax(minUid, qMax(idleHint, watermark));

        if (minUid > watermark)
            m_idleWatcher->maxUidWatermark.store(minUid);
    }

    const bool reconcileDeletes = !announce && !hadIdleHint && minUid > 0;
    const int budget = (minUid == 0) ? Imap::SyncUtils::recentFetchCount() : 0;

    const auto onHeader = [&, email = email](const QVariantMap &h) mutable {
        auto row = h;
        row.insert("accountEmail"_L1, email);

        if (row.value("uid"_L1).toString().isEmpty())
            row.insert("uid"_L1, "real-%1-%2"_L1.arg(email).arg(++seqNum));

        if (row.value("folder"_L1).toString().compare("INBOX"_L1, Qt::CaseInsensitive) == 0)
            ++inboxInserted;

        pendingHeaders.push_back(row);
        resultHeaders.push_back(row);

        if (flushTimer.elapsed() >= 250)
            flush();
    };

    QString fetchStatus;
    const QVariantList headers = fetchFolderHeaders(
        host, port, email, accessToken, &fetchStatus,
        target, onHeader, minUid, reconcileDeletes, budget);

    qInfo().noquote() << "[sync-folder]" << "folder=" << target
                      << "fetched=" << headers.size() << "status=" << fetchStatus.left(80);

    if (isInbox && m_idleWatcher) {
        qint64 maxFetched = 0;
        for (const QVariant &hv : headers) {
            bool ok; const qint64 uid = hv.toMap().value("uid"_L1)
                                             .toString().toLongLong(&ok);
            if (ok && uid > maxFetched) maxFetched = uid;
        }
        if (maxFetched > 0) {
            m_idleWatcher->maxUidWatermark.store(
                qMax(maxFetched, m_idleWatcher->maxUidWatermark.load()));
            m_idleWatcher->minUidHint.store(
                qMax(maxFetched, m_idleWatcher->minUidHint.load()));
        }
    }

    return headers;
}


void
ImapService::refreshFolderList(bool announce) {
    if (m_destroying)
        return;

    if (m_syncInProgress) {
        if (announce)
            emit syncFinished(false, "Sync already in progress.");
        return;
    }

    if (!m_accounts || !m_store) {
        if (announce)
            emit syncFinished(false, "Sync dependencies not initialized.");
        return;
    }

    const auto accounts = m_accounts->accounts();
    if (accounts.isEmpty()) {
        if (announce)
            emit syncFinished(false, "No accounts configured yet.");
        return;
    }

    runAsync(
        [this, accounts]() -> SyncResult {
            qsizetype total = 0;
            QString notes;

            for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Refresh aborted."_L1;
                    return r;
                }

                QString status;
                const auto folders = Imap::SyncEngine::fetchFolders(host, port, email, accessToken, &status);
                total += folders.size();

                for (const QVariant &fv : folders) {
                    const auto f = fv.toMap();
                    QMetaObject::invokeMethod(this, [this, f]() {
                        m_store->upsertFolder(f);
                    }, Qt::QueuedConnection);
                }

                if (notes.isEmpty() && status.contains("Using default Gmail folders."_L1, Qt::CaseInsensitive))
                    notes = " Using default Gmail folders.";
            }

            SyncResult r;
            r.ok      = true;
            r.message = QStringLiteral("Mail folders updated (%1).%2").arg(total).arg(notes);
            return r;
        },
        [this, announce](const SyncResult &r) {
            if (announce && !r.ok)
                emit syncFinished(r.ok, r.message);
        }
    );
}


bool
bodyAlreadyFetched(const DataStore *store, const QString& accountEmail, const QString &folderName, const QString &uid) {
    auto isUsableBodyHtml = [](const QString &value) {
        const auto html = value.trimmed();
        if (html.isEmpty())
            return false;

        const auto lower = html.toLower();
        if (lower.contains("ok success [throttled]"_L1))
            return false;

        if (lower.contains("authenticationfailed"_L1))
            return false;

        static const QRegularExpression htmlRe("<html|<body|<div|<table|<p|<br|<span|<img|<a\\b"_L1,
            QRegularExpression::CaseInsensitiveOption);

        return htmlRe.match(html).hasMatch();
    };

    // Find the existing body_html for this message if it exists
    QVariantMap baseRow;
    for (const auto rows = store->inbox(); const QVariant &v : rows) {
        if (const auto r = v.toMap(); r.value("accountEmail"_L1).toString() == accountEmail &&
                                      r.value("folder"_L1).toString() == folderName &&
                                      r.value("uid"_L1).toString() == uid) {
            baseRow = r;
            break;
                                      }
    }

    if (baseRow.isEmpty() || !isUsableBodyHtml(baseRow.value("bodyHtml"_L1).toString())) {
        return false;
    }

    return true;
}

void
ImapService::hydrateMessageBody(const QString &accountEmail, const QString &folderName, const QString &uid) {
    hydrateMessageBodyInternal(accountEmail, folderName, uid, true);
}

void
ImapService::hydrateMessageBodyInternal(const QString &accountEmail, const QString &folderName, const QString &uid,
                                        const bool userInitiated) {
    const auto emailNorm = accountEmail.trimmed();
    const auto folderNorm = folderName.trimmed();
    const auto uidNorm = uid.trimmed();

    if (m_destroying || !m_accounts || !m_store || emailNorm.isEmpty() || uidNorm.isEmpty())
        return;

    const QString inFlightKey = (emailNorm + "|"_L1 + folderNorm.toLower() + "|"_L1 + uidNorm);
    {
        QMutexLocker locker(&m_inFlightBodyHydrationsMutex);
        if (m_inFlightBodyHydrations.contains(inFlightKey))
            return;
        m_inFlightBodyHydrations.insert(inFlightKey);
    }

    if (bodyAlreadyFetched(m_store, emailNorm, folderNorm, uidNorm)) {
        QMutexLocker locker(&m_inFlightBodyHydrationsMutex);
        m_inFlightBodyHydrations.remove(inFlightKey);
        return;
    }

    const auto accountList = m_accounts->accounts();
    QVariantMap account;
    for (const auto &a : accountList) {
        if (account = a.toMap(); account.value("email"_L1).toString() == emailNorm) {
            break;
        }
    }

    if (account.isEmpty()) {
        {
            QMutexLocker locker(&m_inFlightBodyHydrationsMutex);
            m_inFlightBodyHydrations.remove(inFlightKey);
        }
        qWarning().noquote() << "[hydrate-fail] reason=account-not-found"
                             << "account=" << emailNorm
                             << "folder=" << folderNorm
                             << "uid=" << uidNorm;
        if (userInitiated)
            emit hydrateStatus(false, "Message body fetch failed: account not found.");
        return;
    }

    auto *watcher = new QFutureWatcher<QString>(this);
    registerWatcher(watcher);

    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, emailNorm, folderNorm, uidNorm, inFlightKey, userInitiated]() {
        const QString html = watcher->result();
        unregisterWatcher(watcher);
        watcher->deleteLater();

        {
            QMutexLocker locker(&m_inFlightBodyHydrationsMutex);
            m_inFlightBodyHydrations.remove(inFlightKey);
        }

        if (m_destroying)
            return;

        if (html.isEmpty()) {
            qWarning().noquote() << "[hydrate-fail] reason=empty-body-html"
                                 << "account=" << emailNorm
                                 << "folder=" << folderNorm
                                 << "uid=" << uidNorm;
            if (userInitiated)
                emit hydrateStatus(false, "Message body fetch failed: parser returned empty HTML.");
            return;
        }

        m_store->updateBodyForKey(emailNorm, folderNorm, uidNorm, html);
    });

    watcher->setFuture(QtConcurrent::run([this, account, folderNorm, uidNorm]() -> QString {
        if (m_destroying)
            return {};

        const auto      email = account.value("email"_L1).toString();
        const auto      host  = account.value("imapHost"_L1).toString();
        const qint32    port  = account.value("imapPort"_L1).toInt();

        if (email.isEmpty() || host.isEmpty() || port <= 0) {
            qWarning().noquote() << "[hydrate] abort: invalid account settings"
                                 << "email=" << email << "host=" << host << "port=" << port;
            return {};
        }

        if (account.value("authType"_L1).toString() != "oauth2"_L1) {
            qWarning().noquote() << "[hydrate] abort: non-oauth account" << email;
            return {};
        }

        const auto accessToken = refreshAccessToken(account, email);
        if (accessToken.isEmpty()) {
            qWarning().noquote() << "[hydrate] abort: empty access token" << email;
            return {};
        }

        Imap::MessageHydrator::Request r;
        r.host = host; r.port = port; r.email = email; r.accessToken = accessToken;
        r.folderName = folderNorm; r.uid = uidNorm;

        QVariantList mappedCandidates;
        QMetaObject::invokeMethod(this, [this, &mappedCandidates, email, folderNorm, uidNorm]() {
                if (m_store) mappedCandidates = m_store->fetchCandidatesForMessageKey(email, folderNorm, uidNorm);
            },
        Qt::BlockingQueuedConnection);

        for (const auto &v : mappedCandidates) {
            const auto m = v.toMap();
            r.extraCandidates.push_back( { m.value("folder"_L1).toString(), m.value("uid"_L1).toString() } );
        }

        return Imap::MessageHydrator::execute(r);
    }));
}

void
ImapService::moveMessage(const QString &accountEmail, const QString &folder,
                          const QString &uid, const QString &targetFolder) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty() || targetFolder.isEmpty())
        return;
    if (!m_accounts) return;

    // 1. Snapshot message_id/unread before removing the source edge from the DB.
    qint64 messageId = -1;
    int    unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

        // Optimistic removal: vanish from UI immediately.
        // Moving to trash → remove all non-trash edges (INBOX, All Mail, …).
        // Moving from trash → only remove the specific source edge.
        const QString tgt = targetFolder.trimmed().toLower();
        const bool movingToTrash = tgt == QLatin1String("trash")
                                || tgt == QLatin1String("[gmail]/trash")
                                || tgt == QLatin1String("[google mail]/trash")
                                || tgt.endsWith(QLatin1String("/trash"));
        if (movingToTrash)
            m_store->removeAccountUidsEverywhere(accountEmail, {uid}, /*skipOrphanCleanup=*/true);
        else
            m_store->deleteSingleFolderEdge(accountEmail, folder, uid);
    }

    // 2. UID MOVE on a background thread. Parse COPYUID to get the new UID in the target folder,
    //    then immediately insert the new folder edge so the message appears in the target folder
    //    without waiting for the IDLE watcher to sync.
    const QVariantList accounts = m_accounts->accounts();

    const auto retval = QtConcurrent::run([this, accounts, accountEmail, folder, uid, targetFolder, messageId, unreadVal]() {
        if (m_destroying.load()) return;

        for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
            if (email != accountEmail) continue;

            auto cxn = std::make_shared<Imap::Connection>();
            if (!cxn->connectAndAuth(host, port, email, accessToken).success) return;
            if (const auto [ok, _] = cxn->select(folder); !ok) return;

            // UID MOVE (RFC 6851) — atomic copy+expunge in one round-trip.
            const QString resp = cxn->execute(
                "UID MOVE %1 \"%2\""_L1.arg(uid, targetFolder));

            qInfo().noquote() << "[move-message]"
                              << "uid=" << uid << "from=" << folder
                              << "to=" << targetFolder << "resp=" << resp.simplified().left(120);

            // Parse new UID from: OK [COPYUID <uidvalidity> <source-uid(s)> <dest-uid(s)>]
            static const QRegularExpression kCopyUidRe(
                R"(\[COPYUID\s+\d+\s+\S+\s+(\d+)\])"_L1,
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = kCopyUidRe.match(resp);
            if (m.hasMatch() && messageId > 0) {
                // Server told us the new UID — update DB immediately.
                const QString newUid = m.captured(1);
                QMetaObject::invokeMethod(this,
                    [this, accountEmail, targetFolder, newUid, messageId, unreadVal]() {
                        if (!m_destroying.load() && m_store)
                            m_store->insertFolderEdge(accountEmail, messageId,
                                                      targetFolder, newUid, unreadVal);
                    }, Qt::QueuedConnection);
            } else if (messageId > 0) {
                // Server gave no COPYUID (bad IMAP server). Purge any remaining edges so the
                // UI is clean; the IDLE watcher will re-add the message when the server notifies.
                QMetaObject::invokeMethod(this,
                    [this, accountEmail, messageId]() {
                        if (!m_destroying.load() && m_store)
                            m_store->removeAllEdgesForMessageId(accountEmail, messageId);
                    }, Qt::QueuedConnection);
            }
            return;
        }
    });
}

void
ImapService::markMessageRead(const QString &accountEmail, const QString &folder,
                              const QString &uid) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty()) return;

    // 1. Update DB immediately.
    if (m_store)
        m_store->markMessageRead(accountEmail, uid);

    // 2. Send UID STORE +FLAGS (\Seen) on a background thread.
    if (!m_accounts) return;
    const QVariantList accounts = m_accounts->accounts();

    const auto retval = QtConcurrent::run([this, accounts, accountEmail, folder, uid]() {
        if (m_destroying.load()) return;

        for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
            if (email != accountEmail) continue;

            auto cxn = std::make_shared<Imap::Connection>();
            if (!cxn->connectAndAuth(host, port, email, accessToken).success) return;
            if (const auto [ok, _] = cxn->select(folder); !ok) return;

            const auto result = cxn->execute(QStringLiteral("UID STORE %1 +FLAGS (\\Seen)").arg(uid));

            qInfo().noquote() << "[mark-read]" << "uid=" << uid << "folder=" << folder;
            return;
        }
    });
}

void
ImapService::syncFolder(const QString &folderName, bool announce) {
    if (m_destroying)
        return;

    const auto target = folderName.trimmed().isEmpty() ? "INBOX"_L1 : folderName.trimmed();

    if (target.contains("/Categories/"_L1, Qt::CaseInsensitive)) {
        if (announce)
            emit syncFinished(true, "Category folders are managed via labels.");
        return;
    }

    if (m_syncInProgress) {
        // Announced (user-initiated) requests cancel the active run; background ones queue silently.
        if (announce)
            m_cancelRequested.store(true);

        {
            QMutexLocker l(&m_pendingSyncMutex);
            // Don't let a background single-folder request replace a pending full sync —
            // the full sync will cover this folder anyway.
            if (!m_pendingFullSync || announce) {
                m_pendingFullSync   = false;
                m_pendingFolderSync = target;
                m_pendingAnnounce   = announce;
            }
        }

        return;
    }

    if (!m_accounts || !m_store) {
        if (announce)
            emit syncFinished(false, "Sync dependencies not initialized.");
        return;
    }

    const auto accounts = m_accounts->accounts();
    if (accounts.isEmpty()) {
        if (announce)
            emit syncFinished(false, "No accounts configured yet.");
        return;
    }

    runAsync(
        [this, accounts, target, announce]() -> SyncResult {
            SyncResult result;
            int inboxInserted = 0;
            int seqNum = 0;
            QVariantList pendingHeaders;
            QElapsedTimer flushTimer;
            flushTimer.start();

            auto flush = [&]() {
                if (pendingHeaders.isEmpty())
                    return;

                const QVariantList batch = pendingHeaders; pendingHeaders.clear();
                QMetaObject::invokeMethod(this, [this, batch]() { m_store->upsertHeaders(batch); },
                                         Qt::QueuedConnection);
                flushTimer.restart();
            };

            for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Aborted fetch for %1."_L1.arg(target);
                    return r;
                }

                (void)syncFolderInternal(AccountInfo{email, host, accessToken, port}, target, SyncFolderOptions{announce},
                                           seqNum, inboxInserted, pendingHeaders, result.headers,
                                           flushTimer, flush);
            }

            flush();
            result.ok      = true;
            result.inserted = inboxInserted;
            result.message  = QStringLiteral("%1 sync complete.").arg(target);

            return result;
        },
        [this, announce, target](const SyncResult &r) {
            QString folderLabel = target;
            if (folderLabel.startsWith("[Google Mail]/"_L1, Qt::CaseInsensitive))
                folderLabel = folderLabel.mid(QStringLiteral("[Google Mail]/").size());
            if (folderLabel.startsWith("[Gmail]/"_L1, Qt::CaseInsensitive))
                folderLabel = folderLabel.mid(QStringLiteral("[Gmail]/").size());
            if (folderLabel.compare("INBOX"_L1, Qt::CaseInsensitive) == 0)
                folderLabel = "Inbox"_L1;

            const int syncedCount = r.headers.size();
            if (announce) {
                if (!r.ok)
                    emit syncFinished(false, r.message);
                else if (syncedCount > 0)
                    emit syncFinished(true, QStringLiteral("%1 synced %2 messages.").arg(folderLabel).arg(syncedCount));
                return;
            }

            if (!r.ok) {
                emit realtimeStatus(false, r.message);
            } else if (syncedCount > 0) {
                emit realtimeStatus(true, QStringLiteral("%1 synced %2 messages.").arg(folderLabel).arg(syncedCount));
            }

            if (r.ok && r.inserted > 0) {
                emit realtimeStatus(true, QStringLiteral("%1 new message%2 received.")
                                         .arg(r.inserted)
                                         .arg(r.inserted == 1 ? QString() : "s"));
            }
        }
    );
}

void
ImapService::syncAll(bool announce) {
    if (m_destroying)
        return;

    if (m_syncInProgress) {
        if (announce)
            m_cancelRequested.store(true);

        {
            QMutexLocker l(&m_pendingSyncMutex);
            m_pendingFullSync   = true;
            m_pendingFolderSync.clear();
            m_pendingAnnounce   = announce;
        }

        if (announce)
            emit syncFinished(false, "Refreshing…");

        return;
    }

    if (!m_accounts || !m_store) {
        if (announce)
            emit syncFinished(false, "Sync dependencies not initialized.");

        return;
    }

    const QVariantList accounts = m_accounts->accounts();
    if (accounts.isEmpty()) {
        if (announce)
            emit syncFinished(false, "No accounts configured yet.");
        return;
    }

    runAsync(
        [this, accounts, announce]() -> SyncResult {
            SyncResult result;
            int inboxInserted = 0;
            QVariantList pendingHeaders;
            QElapsedTimer flushTimer;
            flushTimer.start();

            auto flush = [&]() {
                if (pendingHeaders.isEmpty())
                    return;

                const QVariantList batch = pendingHeaders; pendingHeaders.clear();
                QMetaObject::invokeMethod(this, [this, batch]() { m_store->upsertHeaders(batch); },
                                         Qt::QueuedConnection);
                flushTimer.restart();
            };

            for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Refresh aborted.";
                    return r;
                }

                // Fetch and store folder list
                QString folderStatus;
                const auto folders = Imap::SyncEngine::fetchFolders(host, port, email, accessToken, &folderStatus);
                for (const auto &fv : folders) {
                    const auto f = fv.toMap();
                    QMetaObject::invokeMethod(this, [this, f]() { m_store->upsertFolder(f); },
                                             Qt::QueuedConnection);
                }

                // Build ordered sync target list (INBOX first, then all non-category folders)
                QStringList targets = { "INBOX"_L1 };
                QSet<QString> seen  = { "inbox"_L1 };

                QSet<QString> parentContainers;
                for (const auto &fv : folders) {
                    const auto name = fv.toMap().value("name"_L1).toString().trimmed();
                    const int slash = name.indexOf('/');
                    if (slash > 0)
                        parentContainers.insert(name.left(slash).toLower());
                }

                for (const auto &fv : folders) {
                    const auto row = fv.toMap();
                    const auto name = row.value("name"_L1).toString().trimmed();
                    const auto flags = row.value("flags"_L1).toString().toLower();
                    const auto lowerName = name.toLower();

                    const bool isCategory = name.contains("/Categories/"_L1, Qt::CaseInsensitive);
                    const bool isContainerRoot = name.compare("[Gmail]"_L1, Qt::CaseInsensitive) == 0
                                              || name.compare("[Google Mail]"_L1, Qt::CaseInsensitive) == 0;
                    const bool isNoSelect = flags.contains("\\noselect"_L1);
                    const bool isParentContainer = parentContainers.contains(lowerName);

                    if (name.isEmpty() || isCategory || isContainerRoot || isNoSelect || isParentContainer)
                        continue;

                    if (!seen.contains(lowerName)) {
                        seen.insert(lowerName); targets << name;
                    }
                }

                // Gmail fallback: if the folder list was empty, use known Gmail system folders
                if (targets.size() == 1) {
                    for (const char *f : { "[Gmail]/All Mail", "[Gmail]/Sent Mail",
                                           "[Gmail]/Drafts", "[Gmail]/Spam", "[Gmail]/Trash" })
                        targets << QLatin1String(f);
                }

                // Sync each folder through the same canonical folder-sync path.
                int seqNum = 0;
                for (const auto &folderTarget : targets) {
                    if (m_cancelRequested)
                        break;

                    QString folderLabel = folderTarget;
                    if (folderLabel.startsWith("[Google Mail]/"_L1, Qt::CaseInsensitive))
                        folderLabel = folderLabel.mid(QStringLiteral("[Google Mail]/").size());
                    if (folderLabel.startsWith("[Gmail]/"_L1, Qt::CaseInsensitive))
                        folderLabel = folderLabel.mid(QStringLiteral("[Gmail]/").size());
                    if (folderLabel.compare("INBOX"_L1, Qt::CaseInsensitive) == 0)
                        folderLabel = "Inbox"_L1;

                    const auto syncingToast = QStringLiteral("Syncing %1").arg(folderLabel);
                    QMetaObject::invokeMethod(this, [this, announce, syncingToast]() {
                        if (announce)
                            emit syncFinished(true, syncingToast);
                        else
                            emit realtimeStatus(true, syncingToast);
                    }, Qt::QueuedConnection);

                    const auto folderHeaders = syncFolderInternal(AccountInfo{email, host, accessToken, port},
                        folderTarget, SyncFolderOptions{announce}, seqNum, inboxInserted,
                        pendingHeaders, result.headers, flushTimer, flush);

                    QSet<QString> uniqueUids;
                    for (const auto &hv : folderHeaders) {
                        const auto u = hv.toMap().value("uid"_L1).toString().trimmed();
                        if (!u.isEmpty())
                            uniqueUids.insert(u);
                    }
                    const int syncedCount = uniqueUids.size();
                    if (syncedCount <= 0)
                        continue;

                    const auto toast = QStringLiteral("%1 synced %2 messages.")
                                           .arg(folderLabel).arg(syncedCount);
                    QMetaObject::invokeMethod(this, [this, announce, toast]() {
                        if (announce)
                            emit syncFinished(true, toast);
                        else
                            emit realtimeStatus(true, toast);
                    }, Qt::QueuedConnection);
                }
            }

            flush();
            result.ok      = true;
            result.inserted = inboxInserted;
            result.message  = QStringLiteral("All folders synced: %1 inbox messages.").arg(inboxInserted);

            return result;
        },
        [this, announce](const SyncResult &r) {
            if (announce)
                emit syncFinished(r.ok, r.message);
        }
    );
}
