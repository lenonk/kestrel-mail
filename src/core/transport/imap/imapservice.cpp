#include "imapservice.h"

#include "../../accounts/accountrepository.h"
#include "../../auth/tokenvault.h"
#include "../../store/datastore.h"
#include "connection/imapconnection.h"
#include "sync/syncutils.h"
#include "sync/syncengine.h"
#include "message/messagehydrator.h"
#include "message/bodyprocessor.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QProcess>
#include <QSemaphore>
#include <QtConcurrentRun>

#include "sync/idlewatcher.h"
#include "sync/kestreltimer.h"
#include "message/avatarresolver.h"

using namespace Qt::Literals::StringLiterals;
using Imap::AvatarResolver::resolveGooglePeopleAvatarUrl;
using Imap::AvatarResolver::fetchAvatarBlob;
using Imap::AvatarResolver::writeAvatarFile;

namespace {
struct PooledConnSlot {
    std::unique_ptr<Imap::Connection> conn;
    bool busy = false;
    QString email;
    QString host;
    int port = 0;
    QString owner;
    qint64 leasedAtMs = 0;
};

QMutex g_poolMutex;
QWaitCondition g_poolWait;
std::vector<PooledConnSlot> g_poolSlots;
std::atomic_bool g_poolInitialized{false};
std::function<QString(const QString &email)> g_poolTokenRefresher;
// 3 pool + 1 dedicated hydrate + 1 IDLE = 5 total, within Gmail's per-account limit.
constexpr int kOperationalPoolMax = 3;

// Only 1 background body-hydration at a time; user-initiated hydrations bypass this.
QSemaphore g_bgHydrateSem{1};

// Limit concurrent background folder syncs so they can't exhaust the connection pool.
// User-initiated syncs (syncAll / syncFolder with announce=true) bypass this.
QSemaphore g_bgFolderSyncSem{1};
constexpr int kPoolAcquireTimeoutMs = 3500;

bool offlineModeEnabledFromEnv() {
    const QByteArray raw = qgetenv("KESTREL_OFFLINE_MODE").trimmed().toLower();
    if (raw.isEmpty())
        return false;
    return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
}

// Dedicated hydration slot — one per account, established before the pool.
// Used exclusively for user-click-initiated hydration to guarantee availability.
QMutex g_hydrateMutex;
QWaitCondition g_hydrateWait;
std::vector<PooledConnSlot> g_hydrateSlots;

static bool isBackgroundOwner(const QString &owner) {
    return owner.startsWith("bg-", Qt::CaseInsensitive)
        || owner.startsWith("background", Qt::CaseInsensitive);
}
}

std::shared_ptr<Imap::Connection> ImapService::getPooledConnection(const QString &email, const QString &owner) {
    int slotIndex = -1;
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + kPoolAcquireTimeoutMs;

    QMutexLocker lock(&g_poolMutex);
    while (true) {
        int freeForEmail = 0;
        for (int i = 0; i < static_cast<int>(g_poolSlots.size()); ++i) {
            if (!email.isEmpty() && g_poolSlots[i].email.compare(email, Qt::CaseInsensitive) != 0)
                continue;
            if (!g_poolSlots[i].busy)
                ++freeForEmail;
        }

        const bool reserveForUser = isBackgroundOwner(owner) && freeForEmail <= 1;

        if (!reserveForUser) {
            for (int i = 0; i < static_cast<int>(g_poolSlots.size()); ++i) {
                if (g_poolSlots[i].busy)
                    continue;
                if (!email.isEmpty() && g_poolSlots[i].email.compare(email, Qt::CaseInsensitive) != 0)
                    continue;
                g_poolSlots[i].busy = true;
                g_poolSlots[i].owner = owner;
                g_poolSlots[i].leasedAtMs = QDateTime::currentMSecsSinceEpoch();
                slotIndex = i;
                break;
            }
        }

        if (slotIndex >= 0)
            break;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const int remaining = static_cast<int>(deadline - now);
        if (remaining <= 0)
            return {};
        g_poolWait.wait(&g_poolMutex, remaining);
    }

    Imap::Connection *raw = g_poolSlots[slotIndex].conn.get();
    raw->setLogOwner(owner);
    const QString slotEmail = g_poolSlots[slotIndex].email;
    const QString slotHost  = g_poolSlots[slotIndex].host;
    const int     slotPort  = g_poolSlots[slotIndex].port;
    lock.unlock();

    if (!raw->isConnected()) {
        std::function<QString(const QString &)> refresher;
        {
            QMutexLocker rl(&g_poolMutex);
            refresher = g_poolTokenRefresher;
        }
        const QString token = refresher ? refresher(slotEmail) : QString{};
        if (!raw->connectAndAuth(slotHost, slotPort, slotEmail, token).success) {
            QMutexLocker rl(&g_poolMutex);
            g_poolSlots[slotIndex].busy = false;
            g_poolWait.wakeOne();
            return {};
        }
    }

    return {raw, [slotIndex](Imap::Connection *) {
        QMutexLocker rel(&g_poolMutex);
        if (slotIndex >= 0 && slotIndex < static_cast<int>(g_poolSlots.size())) {
            g_poolSlots[slotIndex].busy = false;
            g_poolSlots[slotIndex].owner.clear();
            g_poolSlots[slotIndex].leasedAtMs = 0;
            g_poolWait.wakeOne();
        }
    }};
}

std::shared_ptr<Imap::Connection>
ImapService::getDedicatedHydrateConnection(const QString &email) {
    int slotIndex = -1;
    {
        QMutexLocker lock(&g_hydrateMutex);
        for (int i = 0; i < static_cast<int>(g_hydrateSlots.size()); ++i) {
            if (g_hydrateSlots[i].email.compare(email, Qt::CaseInsensitive) == 0 && !g_hydrateSlots[i].busy) {
                g_hydrateSlots[i].busy = true;
                g_hydrateSlots[i].owner = "hydrate-dedicated"_L1;
                g_hydrateSlots[i].leasedAtMs = QDateTime::currentMSecsSinceEpoch();
                slotIndex = i;
                break;
            }
        }
    }
    if (slotIndex < 0)
        return {};

    Imap::Connection *raw = g_hydrateSlots[slotIndex].conn.get();
    raw->setLogOwner(QStringLiteral("hydrate-dedicated"));
    const QString slotEmail = g_hydrateSlots[slotIndex].email;
    const QString slotHost  = g_hydrateSlots[slotIndex].host;
    const int     slotPort  = g_hydrateSlots[slotIndex].port;

    if (!raw->isConnected()) {
        std::function<QString(const QString &)> refresher;
        {
            QMutexLocker rl(&g_poolMutex);
            refresher = g_poolTokenRefresher;
        }
        const QString token = refresher ? refresher(slotEmail) : QString{};
        if (!raw->connectAndAuth(slotHost, slotPort, slotEmail, token).success) {
            QMutexLocker lock(&g_hydrateMutex);
            g_hydrateSlots[slotIndex].busy = false;
            g_hydrateWait.wakeOne();
            return {};
        }
    }

    return {raw, [slotIndex](Imap::Connection *) {
        QMutexLocker rel(&g_hydrateMutex);
        if (slotIndex >= 0 && slotIndex < static_cast<int>(g_hydrateSlots.size())) {
            g_hydrateSlots[slotIndex].busy = false;
            g_hydrateSlots[slotIndex].owner.clear();
            g_hydrateSlots[slotIndex].leasedAtMs = 0;
            g_hydrateWait.wakeOne();
        }
    }};
}

ImapService::ImapService(AccountRepository *accounts, DataStore *store, TokenVault *vault, QObject *parent)
    : QObject(parent)
    , m_accounts(accounts)
    , m_store(store)
    , m_vault(vault)
    , m_offlineMode(offlineModeEnabledFromEnv()) {
    if (m_offlineMode)
        qInfo() << "[offline-mode] KESTREL_OFFLINE_MODE enabled; IMAP network operations are disabled.";
    Imap::Connection::setThrottleObserver([this](const QString &accountEmail, bool throttled, const QString &response) {
        const QString msg = QStringLiteral("Account throttled by server: %1")
                                .arg(response.simplified().left(240));
        QMetaObject::invokeMethod(this, [this, accountEmail, throttled, msg]() {
            if (m_destroying.load()) return;

            const QString key = accountEmail.trimmed().toLower();
            const bool prev = m_accountThrottleState.value(key, false);
            m_accountThrottleState.insert(key, throttled);

            if (throttled) {
                if (!prev)
                    qInfo().noquote() << "[throttle]" << "account=" << accountEmail << "state=THROTTLED";
                // Drop queued background folder sync work while throttled.
                m_pendingFolderSync.clear();
                emit accountThrottled(accountEmail, msg);
            } else {
                if (prev)
                    qInfo().noquote() << "[throttle]" << "account=" << accountEmail << "state=UNTHROTTLED";
                emit accountUnthrottled(accountEmail);
            }
        }, Qt::QueuedConnection);
    });
}

ImapService::~ImapService() { shutdown(); }

void
ImapService::initialize() {
    {
        QMutexLocker lock(&g_poolMutex);
        g_poolTokenRefresher = [this](const QString &email) -> QString {
            if (!m_accounts) return {};
            for (const auto &a : m_accounts->accounts()) {
                const auto acc = a.toMap();
                if (acc.value("email"_L1).toString().compare(email, Qt::CaseInsensitive) == 0)
                    return refreshAccessToken(acc, email);
            }
            return {};
        };
    }

    if (m_offlineMode) {
        emit realtimeStatus(true, QStringLiteral("Offline mode is enabled (KESTREL_OFFLINE_MODE=1). IMAP sync is paused."));
        return;
    }

    if (!g_poolInitialized.exchange(true)) {

        if (!m_accounts) return;
        const auto accounts = resolveAccounts(m_accounts->accounts());

        // Establish dedicated hydration connections FIRST (one per account).
        // These are reserved for user-click-initiated hydration to guarantee availability.
        for (const auto &[email, host, accessToken, port] : accounts) {
            runBackgroundTask([this, host, email, port, accessToken]() {
                if (m_destroying.load())
                    return;

                auto conn = std::make_unique<Imap::Connection>();
                conn->connectAndAuth(host, port, email, accessToken);

                PooledConnSlot s;
                s.email = email; s.host = host; s.port = port;
                s.conn = std::move(conn); s.busy = false;

                QMutexLocker lock(&g_hydrateMutex);
                g_hydrateSlots.push_back(std::move(s));
                g_hydrateWait.wakeOne();
            });
        }

        // Then establish the general-purpose operational pool.
        for (const auto &[email, host, accessToken, port] : accounts) {
            for (int i = 0; i < kOperationalPoolMax; ++i) {
                runBackgroundTask([this, host, email, port, accessToken]() {
                    if (m_destroying.load())
                        return;

                    auto conn = std::make_unique<Imap::Connection>();
                    conn->connectAndAuth(host, port, email, accessToken);

                    PooledConnSlot s;
                    s.email = email; s.host = host; s.port = port;
                    s.conn = std::move(conn); s.busy = false;

                    QMutexLocker lock(&g_poolMutex);
                    g_poolSlots.push_back(std::move(s));
                    g_poolWait.wakeOne();
                });
            }
        }
    }

    startBackgroundWorker();
    startIdleWatcher();
}

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

    {
        QMutexLocker lock(&g_poolMutex);
        g_poolTokenRefresher = nullptr;
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

QString
ImapService::fileSha256(const QString &localPath) const {
    const QString path = localPath.trimmed();
    if (path.isEmpty())
        return {};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString
ImapService::dataUriSha256(const QString &dataUri) const {
    const QString src = dataUri.trimmed();
    if (!src.startsWith("data:"_L1, Qt::CaseInsensitive))
        return {};

    const int comma = src.indexOf(',');
    if (comma <= 0)
        return {};

    const QString meta = src.left(comma).toLower();
    const QString payload = src.mid(comma + 1);

    QByteArray raw;
    if (meta.contains(";base64"_L1)) {
        raw = QByteArray::fromBase64(payload.toUtf8());
    } else {
        raw = QUrl::fromPercentEncoding(payload.toUtf8()).toUtf8();
    }

    if (raw.isEmpty())
        return {};

    const QByteArray digest = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

QString
ImapService::attachmentPreviewPath(const QString &accountEmail, const QString &uid, const QString &partId,
                                   const QString &fileName, const QString &mimeType) {
    const QString localPath = cachedAttachmentPath(accountEmail, uid, partId);
    if (localPath.isEmpty() || !QFile::exists(localPath))
        return {};

    const QString mt = mimeType.trimmed().toLower();
    const QString lowerName = fileName.trimmed().toLower();
    if (mt.startsWith("image/"_L1) || mt.contains("pdf"_L1)
        || lowerName.endsWith(".pdf"_L1) || lowerName.endsWith(".png"_L1)
        || lowerName.endsWith(".jpg"_L1) || lowerName.endsWith(".jpeg"_L1)
        || lowerName.endsWith(".webp"_L1) || lowerName.endsWith(".gif"_L1)
        || lowerName.endsWith(".bmp"_L1) || lowerName.endsWith(".txt"_L1)
        || lowerName.endsWith(".md"_L1) || lowerName.endsWith(".html"_L1)
        || lowerName.endsWith(".htm"_L1)) {
        return localPath;
    }

    const bool officeLike = mt.contains("officedocument"_L1)
                         || mt.contains("msword"_L1)
                         || mt.contains("vnd.ms"_L1)
                         || lowerName.endsWith(".doc"_L1)
                         || lowerName.endsWith(".docx"_L1)
                         || lowerName.endsWith(".odt"_L1)
                         || lowerName.endsWith(".ppt"_L1)
                         || lowerName.endsWith(".pptx"_L1)
                         || lowerName.endsWith(".xls"_L1)
                         || lowerName.endsWith(".xlsx"_L1);
    if (!officeLike)
        return {};

    QString officeBin = QStandardPaths::findExecutable("libreoffice");
    if (officeBin.isEmpty())
        officeBin = QStandardPaths::findExecutable("soffice");
    if (officeBin.isEmpty())
        return {};

    const QString previewDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + "/attachment-previews/"_L1 + accountEmail + "/"_L1 + uid + "/"_L1 + partId;
    QDir().mkpath(previewDir);

    const QFileInfo inInfo(localPath);
    const QString expected = previewDir + "/"_L1 + inInfo.completeBaseName() + ".png"_L1;
    if (QFile::exists(expected))
        return expected;

    QProcess p;
    p.setProgram(officeBin);
    p.setArguments({"--headless"_L1, "--convert-to"_L1, "png"_L1, "--outdir"_L1, previewDir, localPath});
    p.start();
    if (!p.waitForFinished(8000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};

    if (QFile::exists(expected))
        return expected;

    const QStringList generated = QDir(previewDir).entryList({"*.png"}, QDir::Files, QDir::Name);
    if (!generated.isEmpty())
        return previewDir + "/"_L1 + generated.first();

    return {};
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

static bool isImageAttachment(const QVariantMap &am) {
    const QString mime = am.value("mimeType"_L1).toString().trimmed().toLower();
    const QString name = am.value("name"_L1).toString().trimmed().toLower();
    return mime.startsWith("image/"_L1)
        || name.endsWith(".png"_L1)
        || name.endsWith(".jpg"_L1)
        || name.endsWith(".jpeg"_L1)
        || name.endsWith(".webp"_L1)
        || name.endsWith(".gif"_L1)
        || name.endsWith(".bmp"_L1)
        || name.endsWith(".svg"_L1);
}

void
ImapService::prefetchAttachments(const QString &accountEmail, const QString &folderName, const QString &uid) {
    if (m_offlineMode || !m_store)
        return;

    const QVariantList attachments = m_store->attachmentsForMessage(accountEmail, folderName, uid);
    if (attachments.isEmpty())
        return;

    QVariantList toFetch;
    for (const auto &a : attachments) {
        const auto am = a.toMap();
        if (cachedAttachmentPath(accountEmail, uid, am.value("partId"_L1).toString()).isEmpty())
            toFetch.push_back(a);
    }
    if (toFetch.isEmpty())
        return;

    // Dedup by (account, sorted-partIds): all folder representations of the same
    // message have identical attachment parts. Only one background task needs to
    // run per message — prevents N concurrent pool acquisitions when QML fires
    // one prefetchAttachments call per candidate folder.
    QStringList partIds;
    for (const auto &a : toFetch)
        partIds.push_back(a.toMap().value("partId"_L1).toString());
    std::sort(partIds.begin(), partIds.end());
    const QString taskKey = "task:"_L1 + accountEmail + "|"_L1 + partIds.join(","_L1);

    {
        QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
        if (m_inFlightAttachmentDownloads.contains(taskKey))
            return;
        m_inFlightAttachmentDownloads.insert(taskKey);
    }

    runBackgroundTask([this, accountEmail, folderName, uid, toFetch, taskKey]() {
        struct TaskGuard {
            ImapService *svc;
            QString key;
            ~TaskGuard() {
                QMutexLocker lock(&svc->m_inFlightAttachmentDownloadsMutex);
                svc->m_inFlightAttachmentDownloads.remove(key);
            }
        } taskGuard{this, taskKey};

        auto cxn = getPooledConnection(accountEmail, "prefetch-attachments");
        if (!cxn) {
            qWarning() << "[imap-pool] timed out acquiring operational connection for" << accountEmail;
            return;
        }
        if (cxn->selectedFolder().compare(folderName, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->examine(folderName);
            if (!ok) {
                    return;
            }
        }

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

            {
                QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
                if (m_inFlightAttachmentDownloads.contains(key))
                    continue;
                m_inFlightAttachmentDownloads.insert(key);
            }

            const auto clearInFlight = [this, key]() {
                QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
                m_inFlightAttachmentDownloads.remove(key);
            };

            if (!cachedAttachmentPath(accountEmail, uid, partId).isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
                clearInFlight();
                continue;
            }

            emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
            const QByteArray data = fetchAttachmentPartById(*cxn, uid, partId, encoding,
                [this, &accountEmail, &uid, &partId](const int percent, const qint64) {
                    emit attachmentDownloadProgress(accountEmail, uid, partId, percent);
                });
            if (data.isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
                clearInFlight();
                continue;
            }

            const QString safePartId = QString(partId).replace('/', '_').replace('.', '_');
            const QString partDir    = cacheBase + "/"_L1 + safePartId;
            QDir().mkpath(partDir);

            const QString safeName  = QString(name).replace(QRegularExpression("[^A-Za-z0-9._-]"_L1), "_"_L1);
            const QString localPath = partDir + "/"_L1 + (safeName.isEmpty() ? "attachment"_L1 : safeName);

            QSaveFile f(localPath);
            if (!f.open(QIODevice::WriteOnly)) {
                clearInFlight();
                continue;
            }
            f.write(data);
            if (!f.commit()) {
                clearInFlight();
                continue;
            }

            attachmentCacheInsert(key, localPath);
            emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
            emit attachmentReady(accountEmail, uid, partId, localPath);
            clearInFlight();
        }
    });
}

void
ImapService::prefetchImageAttachments(const QString &accountEmail, const QString &folderName, const QString &uid) {
    if (m_offlineMode || !m_store)
        return;

    const QVariantList attachments = m_store->attachmentsForMessage(accountEmail, folderName, uid);
    if (attachments.isEmpty())
        return;

    QVariantList toFetch;
    for (const auto &a : attachments) {
        const auto am = a.toMap();
        if (!isImageAttachment(am))
            continue;
        if (cachedAttachmentPath(accountEmail, uid, am.value("partId"_L1).toString()).isEmpty())
            toFetch.push_back(a);
    }
    if (toFetch.isEmpty())
        return;

    runBackgroundTask([this, accountEmail, folderName, uid, toFetch]() {
        auto cxn = getPooledConnection(accountEmail, "prefetch-images");
        if (!cxn) {
            qWarning() << "[imap-pool] timed out acquiring operational connection for" << accountEmail;
            return;
        }
        if (cxn->selectedFolder().compare(folderName, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->examine(folderName);
            if (!ok) {
                    return;
            }
        }

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

            {
                QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
                if (m_inFlightAttachmentDownloads.contains(key))
                    continue;
                m_inFlightAttachmentDownloads.insert(key);
            }

            const auto clearInFlight = [this, key]() {
                QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
                m_inFlightAttachmentDownloads.remove(key);
            };

            if (!cachedAttachmentPath(accountEmail, uid, partId).isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
                clearInFlight();
                continue;
            }

            emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
            const QByteArray data = fetchAttachmentPartById(*cxn, uid, partId, encoding,
                [this, &accountEmail, &uid, &partId](const int percent, const qint64) {
                    emit attachmentDownloadProgress(accountEmail, uid, partId, percent);
                });
            if (data.isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
                clearInFlight();
                continue;
            }

            const QString safePartId = QString(partId).replace('/', '_').replace('.', '_');
            const QString partDir    = cacheBase + "/"_L1 + safePartId;
            QDir().mkpath(partDir);

            const QString safeName  = QString(name).replace(QRegularExpression("[^A-Za-z0-9._-]"_L1), "_"_L1);
            const QString localPath = partDir + "/"_L1 + (safeName.isEmpty() ? "attachment"_L1 : safeName);

            QSaveFile f(localPath);
            if (!f.open(QIODevice::WriteOnly)) {
                clearInFlight();
                continue;
            }
            f.write(data);
            if (!f.commit()) {
                clearInFlight();
                continue;
            }

            attachmentCacheInsert(key, localPath);
            emit attachmentDownloadProgress(accountEmail, uid, partId, 100);
            emit attachmentReady(accountEmail, uid, partId, localPath);
            clearInFlight();
        }
    });
}

static QByteArray
fetchAttachmentPartById(Imap::Connection &cxn, const QString &uid, const QString &partId, const QString &encoding,
                        const std::function<void(int, qint64)> &onProgress) {
    QString status;
    QByteArray literal = cxn.fetchMimePartWithProgress(uid, partId, 1, onProgress, &status);
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
    const int prev = m_syncInProgress.fetch_add(1);
    if (prev == 0)
        emit syncActivityChanged(true);

    auto *watcher = new QFutureWatcher<SyncResult>(this);
    registerWatcher(watcher);
    connect(watcher, &QFutureWatcher<SyncResult>::finished, this,
            [this, watcher, onDone = std::move(onDone)]() {
        const auto r = watcher->result();

        if (!m_destroying && onDone)
            onDone(r);

        const int now = m_syncInProgress.fetch_sub(1) - 1;
        if (now <= 0)
            emit syncActivityChanged(false);

        unregisterWatcher(watcher);
        watcher->deleteLater();

    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
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
                if (out && m_store) *out = m_store->folderUids(email, folder);
            }, Qt::DirectConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::pruneFolderToUidsRequested,
            this, [this](const QString &email, const QString &folder, const QStringList &uids) {
                if (m_store) m_store->pruneFolderToUids(email, folder, uids);
            }, Qt::DirectConnection);

    connect(m_idleWatcher, &Imap::IdleWatcher::removeUidsRequested,
            this, [this](const QString &email, const QStringList &uids) {
                if (m_store) m_store->removeAccountUidsEverywhere(email, uids);
            }, Qt::DirectConnection);

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

    // Call stop() directly — it only stores to an atomic_bool so it is safe
    // to call from any thread.  A QueuedConnection would never be delivered
    // because start() runs a tight blocking loop that never yields to the
    // thread's event loop.
    m_idleWatcher->stop();
    m_idleThread->quit();

    if (waitForStop) {
        // Pump the main-thread event loop in short bursts while waiting.
        // This prevents a deadlock if the idle thread is currently blocked on a
        // BlockingQueuedConnection signal (requestAccounts / requestRefreshAccessToken)
        // waiting for the main thread to respond.
        while (!m_idleThread->wait(100))
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
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

    connect(m_backgroundWorker, &Imap::BackgroundWorker::requestAccountThrottled,
            this, [this](const QString &accountEmail, bool *out) {
                if (!out) return;
                *out = m_accountThrottleState.value(accountEmail.trimmed().toLower(), false);
            }, Qt::BlockingQueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::upsertFoldersRequested,
            this, [this](const QVariantList &folders) {
                if (!m_store) return;
                for (const QVariant &fv : folders)
                    m_store->upsertFolder(fv.toMap());
            }, Qt::DirectConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::loadFolderStatusSnapshotRequested,
            this, [this](const QString &accountEmail, const QString &folder,
                         qint64 *uidNext, qint64 *highestModSeq, qint64 *messages, bool *found) {
                if (!m_store) { if (found) *found = false; return; }
                const auto row = m_store->folderSyncStatus(accountEmail, folder);
                const bool exists = !row.isEmpty();
                if (found) *found = exists;
                if (!exists) return;
                if (uidNext) *uidNext = row.value("uidNext"_L1).toLongLong();
                if (highestModSeq) *highestModSeq = row.value("highestModSeq"_L1).toLongLong();
                if (messages) *messages = row.value("messages"_L1).toLongLong();
            }, Qt::DirectConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::saveFolderStatusSnapshotRequested,
            this, [this](const QString &accountEmail, const QString &folder,
                         const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) {
                if (m_store)
                    m_store->upsertFolderSyncStatus(accountEmail, folder, uidNext, highestModSeq, messages);
            }, Qt::DirectConnection);


    connect(m_backgroundWorker, &Imap::BackgroundWorker::syncHeadersAndFlagsRequested,
            this, [this](const QVariantMap &account, const QString &email, const QString &folder,
                         const QString &accessToken) {
                backgroundSyncHeadersAndFlags(account, email, folder, accessToken);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::idleLiveUpdateRequested,
            this, [this](const QVariantMap &account, const QString &email) {
                backgroundOnIdleLiveUpdate(account, email);
            }, Qt::QueuedConnection);

    connect(m_backgroundWorker, &Imap::BackgroundWorker::loopError,
            this, [this](const QString &message) {
                workerEmitRealtimeStatus(false, message);
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

    // Same reasoning as stopIdleWatcher: call stop() directly on the atomic.
    m_backgroundWorker->stop();
    m_backgroundThread->quit();

    if (waitForStop) {
        while (!m_backgroundThread->wait(100))
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
}

QVariantList
ImapService::workerGetAccounts() const {
    return m_accounts ? m_accounts->accounts() : QVariantList{};
}

QString
ImapService::workerRefreshAccessToken(const QVariantMap &account, const QString &email) {
    return refreshAccessToken(account, email);
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

    const QString folderNorm = folder.trimmed();
    if (folderNorm.isEmpty())
        return;

    const QString folderLower = folderNorm.toLower();
    const bool isInboxish = (folderLower == QStringLiteral("inbox")
                             || folderLower == QStringLiteral("[gmail]/inbox")
                             || folderLower == QStringLiteral("[google mail]/inbox")
                             || folderLower.endsWith(QStringLiteral("/inbox")));
    // Product policy: background body hydration only for Inbox-class folders.
    if (!isInboxish)
        return;

    if (m_accountThrottleState.value(email.trimmed().toLower(), false)) {
        static QHash<QString, qint64> s_lastThrottleSkipLogMs;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QString k = email.trimmed().toLower();
        if (!s_lastThrottleSkipLogMs.contains(k) || (nowMs - s_lastThrottleSkipLogMs.value(k)) > 60000) {
            s_lastThrottleSkipLogMs.insert(k, nowMs);
            qInfo().noquote() << "[bg-hydrate]" << "account=" << email << "paused=true reason=throttled";
        }
        return;
    }

    const QString key = email.trimmed().toLower() + "|" + folderNorm.toLower();
    {
        QMutexLocker lock(&m_bgHydrateMutex);
        if (m_activeBgHydrateFolders.contains(key)) {
            m_pendingBgHydrateFolders.insert(key);
            return;
        }
        m_activeBgHydrateFolders.insert(key);
    }

    bool ok = false;
    const int configuredLimit = qEnvironmentVariableIntValue("KESTREL_BG_BODY_FETCH_LIMIT", &ok);
    const int limit = ok && configuredLimit > 0 ? configuredLimit : 8;

    runBackgroundTask([this, email, folderNorm, key, limit]() {
        int noProgressPasses = 0;
        int processedThisRun = 0;

        bool chunkOk = false;
        const int configuredChunk = qEnvironmentVariableIntValue("KESTREL_BG_HYDRATE_PER_LOOP", &chunkOk);
        const int maxPerLoop = chunkOk ? qBound(25, configuredChunk, 50) : 40;

        while (!m_destroying.load() && processedThisRun < maxPerLoop) {
            QStringList candidates;
            if (m_store)
                candidates = m_store->bodyFetchCandidates(email, folderNorm, limit);

            if (candidates.isEmpty())
                break;

            int hydratedThisPass = 0;
            for (const QString &uid : candidates) {
                if (m_destroying.load() || processedThisRun >= maxPerLoop)
                    break;

                const QString inFlightKey = email.trimmed() + "|"_L1 + folderNorm.toLower() + "|"_L1 + uid.trimmed();

                QMetaObject::invokeMethod(this,
                                          [this, email, folderNorm, uid]() {
                                              hydrateMessageBodyInternal(email, folderNorm, uid, false);
                                          },
                                          Qt::QueuedConnection);

                // Pace hydration and keep exactly one background fetch active at a time.
                // Wait for this UID's in-flight marker to clear before moving on.
                const qint64 waitStartMs = QDateTime::currentMSecsSinceEpoch();
                while (!m_destroying.load()) {
                    bool stillInFlight = false;
                    {
                        QMutexLocker inFlightLock(&m_inFlightBodyHydrationsMutex);
                        stillInFlight = m_inFlightBodyHydrations.contains(inFlightKey);
                    }
                    if (!stillInFlight)
                        break;
                    if ((QDateTime::currentMSecsSinceEpoch() - waitStartMs) > 90'000)
                        break;
                    QThread::msleep(120);
                }

                ++processedThisRun;

                if (m_store) {
                    const QVariantMap row = m_store->messageByKey(email, folderNorm, uid);
                    const QString body = row.value("bodyHtml"_L1).toString();
                    if (!body.trimmed().isEmpty()
                        && !body.contains("ok success [throttled]"_L1, Qt::CaseInsensitive)
                        && !body.contains("authenticationfailed"_L1, Qt::CaseInsensitive)) {
                        ++hydratedThisPass;
                    }
                }

                QThread::msleep(500);
            }

            if (hydratedThisPass <= 0) {
                ++noProgressPasses;
                if (noProgressPasses >= 3) {
                    qInfo().noquote() << "[bg-hydrate]" << "account=" << email
                                      << "folder=" << folderNorm
                                      << "stopping=true reason=no-progress";
                    break;
                }
                QThread::msleep(2500);
            } else {
                noProgressPasses = 0;
                QThread::msleep(1200);
            }
        }

        if (processedThisRun >= maxPerLoop) {
            qInfo().noquote() << "[bg-hydrate]" << "account=" << email
                              << "folder=" << folderNorm
                              << "processed=" << processedThisRun
                              << "maxPerLoop=" << maxPerLoop;
        }

        QMutexLocker lock(&m_bgHydrateMutex);
        m_activeBgHydrateFolders.remove(key);
        m_pendingBgHydrateFolders.remove(key);
    });
}


void
ImapService::backgroundOnIdleLiveUpdate(const QVariantMap &, const QString &email) {
    auto fn = [this, email]() {
        if (m_destroying)
            return;

        if (!m_idleWatcher || !m_idleWatcher->isRunning()) {
            startIdleWatcher();
            emit realtimeStatus(true, QStringLiteral("Idle/live watcher (re)started."));
        }

        // Keep background hydration progressing even when folder STATUS values are stable
        // and no header sync is dispatched in this loop. Inbox-only policy is enforced
        // inside backgroundFetchBodies().
        if (!email.trimmed().isEmpty())
            backgroundFetchBodies({}, email, QStringLiteral("INBOX"), {});
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

QVariantMap
ImapService::loadFolderStatusSnapshot(const QString &accountEmail, const QString &folder) const {
    return m_store ? m_store->folderSyncStatus(accountEmail, folder) : QVariantMap{};
}

void
ImapService::saveFolderStatusSnapshot(const QString &accountEmail, const QString &folder,
                                      const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) {
    if (m_store)
        m_store->upsertFolderSyncStatus(accountEmail, folder, uidNext, highestModSeq, messages);
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
ImapService::fetchFolderHeaders(const QString &email,
                                QString *statusOut,
                                const QString &folderName,
                                const std::function<void(const QVariantMap &)> &onHeader,
                                qint64 minUidExclusive,
                                bool reconcileDeletes,
                                int fetchBudget) {
    auto pooled = getPooledConnection(email, "fetch-folder-headers");
    if (!pooled) {
        if (statusOut)
            *statusOut = "Operational IMAP pool timeout."_L1;
        return {};
    }

    Imap::SyncContext ctx;
    ctx.cxn                     = pooled;
    ctx.folderName              = folderName;
    ctx.minUidExclusive         = minUidExclusive;
    ctx.reconcileDeletes        = reconcileDeletes;
    ctx.fetchBudget             = fetchBudget;
    ctx.cancelRequested         = &m_cancelRequested;
    ctx.onHeader                = onHeader;

    ctx.avatarShouldRefresh = [this](const QString &senderEmail, int ttlSecs, int maxFailures) -> bool {
        return m_store ? m_store->avatarShouldRefresh(senderEmail, ttlSecs, maxFailures) : true;
    };

    ctx.getFolderUids = [this](const QString &acctEmail, const QString &folder) -> QStringList {
        return m_store ? m_store->folderUids(acctEmail, folder) : QStringList{};
    };

    ctx.getFolderMessageCount = [this](const QString &acctEmail, const QString &folder) -> qint64 {
        return m_store ? m_store->folderMessageCount(acctEmail, folder) : -1;
    };

    ctx.getUidsNeedingSnippetRefresh = [this](const QString &acctEmail, const QString &folder) -> QStringList {
        return m_store ? m_store->folderUidsWithNullSnippet(acctEmail, folder) : QStringList{};
    };

    ctx.pruneFolder = [this](const QString &acctEmail, const QString &folder, const QStringList &remoteUids) {
        if (m_store) m_store->pruneFolderToUids(acctEmail, folder, remoteUids);
    };

    ctx.onFlagsReconciled = [this](const QString &acctEmail, const QString &folder,
                                    const QStringList &readUids) {
        if (m_store) m_store->reconcileReadFlags(acctEmail, folder, readUids);
    };

    ctx.onFlaggedReconciled = [this](const QString &acctEmail, const QString &folder,
                                     const QStringList &flaggedUids) {
        if (m_store) m_store->reconcileFlaggedUids(acctEmail, folder, flaggedUids);
    };

    ctx.lookupByMessageIdHeaders = [this](const QString &acctEmail, const QStringList &msgIds) {
        return m_store ? m_store->lookupByMessageIdHeaders(acctEmail, msgIds) : QMap<QString,qint64>{};
    };
    ctx.insertFolderEdge = [this](const QString &acctEmail, qint64 msgId,
                                   const QString &folder, const QString &uid, int unread) {
        if (m_store) m_store->insertFolderEdge(acctEmail, msgId, folder, uid, unread);
    };

    // CONDSTORE: load the modseq we recorded at the end of the previous sync for this
    // folder.  SyncEngine compares it with the fresh EXAMINE HIGHESTMODSEQ to decide
    // whether any server-side changes occurred since our last sync.
    ctx.lastHighestModSeq = m_store ? m_store->folderLastSyncModSeq(email, folderName) : 0;
    ctx.onSyncStateUpdated = [this, email, folderName](qint64 modseq) {
        if (m_store) m_store->updateFolderLastSyncModSeq(email, folderName, modseq);
    };

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
    const bool announce = options.announce;

    const qint64 dbMaxUid = m_store ? m_store->folderMaxUid(email, target) : 0;

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
    const int fetchTarget = Imap::SyncUtils::recentFetchCount();
    int budget;
    if (minUid == 0) {
        // First sync: fetch up to fetchTarget newest messages (-1 = unlimited).
        budget = fetchTarget;
    } else {
        // Incremental sync: backfill older messages, but only once per session per folder.
        const QString folderKey = target.trimmed().toLower();
        bool shouldBackfill = false;
        {
            QMutexLocker lock(&m_backfilledFoldersMutex);
            if (!m_backfilledFolders.contains(folderKey)) {
                m_backfilledFolders.insert(folderKey);
                shouldBackfill = true;
            }
        }

        if (!shouldBackfill) {
            budget = 0;
        } else if (fetchTarget < 0) {
            // No target set — backfill everything missing.
            budget = -1;
        } else {
            // Allow a full fetchTarget-sized backfill batch each session.
            // SyncEngine's internal check (localCount == remoteExists via EXAMINE) skips
            // UID SEARCH ALL if we already have everything, so this is safe to leave open.
            budget = fetchTarget;
        }
    }

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
        email, &fetchStatus,
        target, onHeader, minUid, reconcileDeletes, budget);


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

    if (m_offlineMode) {
        if (announce)
            emit syncFinished(true, QStringLiteral("Offline mode: skipped folder refresh."));
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

            for (const auto &info : resolveAccounts(accounts)) {
                const auto &email = info.email;
                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Refresh aborted."_L1;
                    return r;
                }

                auto pooled = getPooledConnection(email, "refresh-folder-list");
                if (!pooled) continue;

                QString status;
                const auto folders = Imap::SyncEngine::fetchFolders(pooled, &status, true);
                pooled.reset();
                total += folders.size();

                for (const QVariant &fv : folders) {
                    if (m_store) m_store->upsertFolder(fv.toMap());
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

void
ImapService::refreshGoogleCalendars() {
    if (m_destroying || !m_accounts)
        return;

    const auto accounts = m_accounts->accounts();
    if (accounts.isEmpty()) {
        if (!m_googleCalendarList.isEmpty()) {
            m_googleCalendarList.clear();
            emit googleCalendarListChanged();
        }
        return;
    }

    runBackgroundTask([this, accounts]() {
        QString accessToken;
        for (const auto &info : resolveAccounts(accounts)) {
            if (info.host.contains("gmail", Qt::CaseInsensitive)) {
                accessToken = info.accessToken;
                break;
            }
        }
        if (accessToken.isEmpty())
            return;

        QNetworkAccessManager nam;
        QUrl listUrl(QStringLiteral("https://www.googleapis.com/calendar/v3/users/me/calendarList"));
        QUrlQuery listQuery;
        listQuery.addQueryItem(QStringLiteral("colorRgbFormat"), QStringLiteral("true"));
        listUrl.setQuery(listQuery);
        QNetworkRequest req{listUrl};
        req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QEventLoop loop;
        QNetworkReply *reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray payload = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString err = reply->errorString();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString bodyText = QString::fromUtf8(payload).trimmed();
        reply->deleteLater();
        if (!ok) {
            qWarning().noquote() << "[calendar][google] calendarList fetch failed"
                                 << "httpStatus=" << httpStatus
                                 << "error=" << err
                                 << "body=" << bodyText;
            QMetaObject::invokeMethod(this, [this, httpStatus]() {
                Q_UNUSED(httpStatus)
                emit realtimeStatus(false, QStringLiteral("Calendar sync error. Please reconnect your Google account and try again."));
            }, Qt::QueuedConnection);
            return;
        }

        const auto doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            return;

        QVariantList list;
        const auto items = doc.object().value("items").toArray();
        list.reserve(items.size());
        for (const auto &v : items) {
            const auto o = v.toObject();
            QVariantMap row;
            row.insert("id", o.value("id").toString());
            row.insert("name", o.value("summary").toString());
            row.insert("color", o.value("backgroundColor").toString());
            row.insert("checked", true);
            row.insert("account", "gmail");
            list.push_back(row);
        }

        QMetaObject::invokeMethod(this, [this, list]() {
            m_googleCalendarList = list;
            emit googleCalendarListChanged();
        }, Qt::QueuedConnection);
    });
}

void
ImapService::refreshGoogleWeekEvents(const QStringList &calendarIds,
                                     const QString &weekStartIso,
                                     const QString &weekEndIso) {
    if (m_destroying || !m_accounts)
        return;

    const auto accounts = m_accounts->accounts();
    if (accounts.isEmpty() || calendarIds.isEmpty()) {
        if (!m_googleWeekEvents.isEmpty()) {
            m_googleWeekEvents.clear();
            emit googleWeekEventsChanged();
        }
        return;
    }

    runBackgroundTask([this, accounts, calendarIds, weekStartIso, weekEndIso]() {
        QString accessToken;
        for (const auto &info : resolveAccounts(accounts)) {
            if (info.host.contains("gmail", Qt::CaseInsensitive)) {
                accessToken = info.accessToken;
                break;
            }
        }
        if (accessToken.isEmpty())
            return;

        const QDateTime weekStart = QDateTime::fromString(weekStartIso, Qt::ISODate);
        const QDateTime weekEnd = QDateTime::fromString(weekEndIso, Qt::ISODate);
        if (!weekStart.isValid() || !weekEnd.isValid())
            return;

        QNetworkAccessManager nam;
        QVariantList out;

        for (const QString &calendarId : calendarIds) {
            QUrl evUrl(QStringLiteral("https://www.googleapis.com/calendar/v3/calendars/%1/events")
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId))));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("singleEvents"), QStringLiteral("true"));
            q.addQueryItem(QStringLiteral("orderBy"), QStringLiteral("startTime"));
            q.addQueryItem(QStringLiteral("timeMin"), weekStart.toUTC().toString(Qt::ISODate));
            q.addQueryItem(QStringLiteral("timeMax"), weekEnd.toUTC().toString(Qt::ISODate));
            evUrl.setQuery(q);

            QNetworkRequest req{evUrl};
            req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

            QEventLoop loop;
            QNetworkReply *reply = nam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString bodyText = QString::fromUtf8(payload).trimmed();
            reply->deleteLater();
            if (!ok) {
                qWarning().noquote() << "[calendar][google] events fetch failed"
                                     << "calendarId=" << calendarId
                                     << "httpStatus=" << httpStatus
                                     << "error=" << err
                                     << "body=" << bodyText;
                QMetaObject::invokeMethod(this, [this, calendarId]() {
                    Q_UNUSED(calendarId)
                    emit realtimeStatus(false, QStringLiteral("Calendar events couldn’t be loaded right now. Please try again."));
                }, Qt::QueuedConnection);
                continue;
            }

            const auto doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject())
                continue;

            const auto items = doc.object().value("items").toArray();
            for (const auto &v : items) {
                const auto o = v.toObject();
                const auto startObj = o.value("start").toObject();
                const auto endObj = o.value("end").toObject();

                const QString startDateTimeStr = startObj.value("dateTime").toString();
                const QString endDateTimeStr = endObj.value("dateTime").toString();
                const QString startDateStr = startObj.value("date").toString();
                const QString endDateStr = endObj.value("date").toString();

                QDateTime startDt;
                QDateTime endDt;
                bool isAllDay = false;

                if (!startDateTimeStr.isEmpty() && !endDateTimeStr.isEmpty()) {
                    startDt = QDateTime::fromString(startDateTimeStr, Qt::ISODate);
                    endDt = QDateTime::fromString(endDateTimeStr, Qt::ISODate);
                } else if (!startDateStr.isEmpty() && !endDateStr.isEmpty()) {
                    // Google all-day end.date is exclusive.
                    const QDate s = QDate::fromString(startDateStr, Qt::ISODate);
                    const QDate eExclusive = QDate::fromString(endDateStr, Qt::ISODate);
                    if (s.isValid() && eExclusive.isValid()) {
                        isAllDay = true;
                        const QTimeZone localTz = QTimeZone::systemTimeZone();
                        startDt = QDateTime(s, QTime(0, 0, 0), localTz);
                        endDt = QDateTime(eExclusive, QTime(0, 0, 0), localTz);
                    }
                }

                if (!startDt.isValid() || !endDt.isValid())
                    continue;

                const qint64 dayIndex = weekStart.date().daysTo(startDt.date());
                if (dayIndex < 0 || dayIndex > 6)
                    continue;

                const int minutes = isAllDay ? 0 : (startDt.time().hour() * 60 + startDt.time().minute());
                const int durMinutes = isAllDay
                                      ? (24 * 60)
                                      : qMax(15, static_cast<int>(startDt.secsTo(endDt) / 60));

                QVariantMap row;
                row.insert("calendarId", calendarId);
                row.insert("dayIndex", static_cast<int>(dayIndex));
                row.insert("startHour", static_cast<double>(minutes) / 60.0);
                row.insert("durationHours", static_cast<double>(durMinutes) / 60.0);
                row.insert("title", o.value("summary").toString());
                row.insert("subtitle", isAllDay
                           ? QStringLiteral("All day")
                           : QStringLiteral("%1 - %2")
                                 .arg(startDt.time().toString("h:mmap").toLower())
                                 .arg(endDt.time().toString("h:mmap").toLower()));
                out.push_back(row);
            }
        }

        QMetaObject::invokeMethod(this, [this, out]() {
            m_googleWeekEvents = out;
            emit googleWeekEventsChanged();
        }, Qt::QueuedConnection);
    });
}


bool
bodyAlreadyFetched(const DataStore *store, const QString& accountEmail, const QString &folderName, const QString &uid) {
    if (!store)
        return false;
    return store->hasUsableBodyForEdge(accountEmail, folderName, uid);
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

    if (m_offlineMode) {
        if (userInitiated)
            emit hydrateStatus(false, QStringLiteral("Offline mode is enabled. Body hydration is paused."));
        return;
    }

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
            qWarning().noquote() << "[hydrate-html-db] empty-result"
                                 << "source=" << (userInitiated ? "user" : "bg")
                                 << "account=" << emailNorm
                                 << "folder=" << folderNorm
                                 << "uid=" << uidNorm;
            if (userInitiated)
                emit hydrateStatus(false, "Message body fetch failed: parser returned empty HTML.");
            return;
        }

        const QString htmlTrim = html.trimmed();
        const QString htmlLower = htmlTrim.left(1024).toLower();
        const bool hasHtmlish = QRegularExpression(QStringLiteral("<html|<body|<div|<table|<p|<br|<span|<img|<a\\b"),
                                                 QRegularExpression::CaseInsensitiveOption).match(htmlTrim).hasMatch();
        const bool hasMarkdownLinks = QRegularExpression(QStringLiteral("\\[[^\\]\\n]{1,240}\\]\\(https?://[^\\s)]+\\)"),
                                                         QRegularExpression::CaseInsensitiveOption).match(htmlTrim).hasMatch();
        const bool hasMimeHeaders = htmlLower.contains(QStringLiteral("content-type:"))
                                 || htmlLower.contains(QStringLiteral("mime-version:"));
        const bool plainWrap = htmlTrim.startsWith("<html><body style=\"white-space:normal;\">", Qt::CaseInsensitive);
        const bool suspicious = !hasHtmlish || hasMarkdownLinks || hasMimeHeaders || htmlTrim.size() < 160;

        QVariantMap row;
        if (m_store)
            row = m_store->messageByKey(emailNorm, folderNorm, uidNorm);
        const QString subject = row.value("subject"_L1).toString();
        const QString prevHtml = row.value("bodyHtml"_L1).toString().trimmed();
        const bool prevPlainWrap = prevHtml.startsWith("<html><body style=\"white-space:normal;\">", Qt::CaseInsensitive);
        const bool prevRichHtml = prevHtml.contains("<table"_L1, Qt::CaseInsensitive)
                               || prevHtml.contains("<style"_L1, Qt::CaseInsensitive)
                               || prevHtml.contains("<head"_L1, Qt::CaseInsensitive);

        qWarning().noquote() << "[hydrate-html-db] hydrate-result"
                             << "source=" << (userInitiated ? "user" : "bg")
                             << "uid=" << uidNorm
                             << "subject=" << subject.left(120)
                             << "htmlLen=" << htmlTrim.size()
                             << "hasHtmlish=" << hasHtmlish
                             << "hasMarkdownLinks=" << hasMarkdownLinks
                             << "hasMimeHeaders=" << hasMimeHeaders
                             << "plainWrap=" << plainWrap
                             << "suspicious=" << suspicious;

        if (plainWrap && prevRichHtml && !prevPlainWrap && prevHtml.size() > 512) {
            qWarning().noquote() << "[hydrate-html-db] skip-downgrade"
                                 << "source=" << (userInitiated ? "user" : "bg")
                                 << "uid=" << uidNorm
                                 << "prevLen=" << prevHtml.size()
                                 << "newLen=" << htmlTrim.size();
            return;
        }

        m_store->updateBodyForKey(emailNorm, folderNorm, uidNorm, html);
    });

    const QString emailCopy = emailNorm;
    watcher->setFuture(QtConcurrent::run([this, account, folderNorm, uidNorm, emailCopy, userInitiated]() -> QString {
        const QElapsedTimer hydrateTimer = [](){ QElapsedTimer t; t.start(); return t; }();
        if (m_destroying)
            return {};

        if (account.value("authType"_L1).toString() != "oauth2") {
            return {};
        }

        // Serialize all background hydrations to a single connection slot so
        // the pool stays available for user actions, syncs, and IDLE.
        // User-initiated hydrations skip this entirely.
        const bool isBg = !userInitiated;
        if (isBg && !g_bgHydrateSem.tryAcquire(1, 60'000))
            return {};
        struct SemGuard {
            bool active;
            ~SemGuard() { if (active) g_bgHydrateSem.release(); }
        } semGuard{isBg};

        Imap::MessageHydrator::Request r;
        r.folderName = folderNorm; r.uid = uidNorm;

        QVariantList mappedCandidates;
        if (m_store)
            mappedCandidates = m_store->fetchCandidatesForMessageKey(emailCopy, folderNorm, uidNorm);

        for (const auto &v : mappedCandidates) {
            const auto m = v.toMap();
            r.extraCandidates.push_back( { m.value("folder"_L1).toString(), m.value("uid"_L1).toString() } );
        }

        const qint64 mapMs = hydrateTimer.elapsed();

        std::shared_ptr<Imap::Connection> pooled;
        bool usedDedicated = false;
        if (userInitiated) {
            // Try the dedicated hydration connection first (non-blocking).
            pooled = getDedicatedHydrateConnection(emailCopy);
            if (pooled) {
                usedDedicated = true;
            } else {
                // Dedicated slot busy — fall back to pool.
                qint8 attempts = 0;
                pooled = getPooledConnection(emailCopy, "hydrate-user"_L1);
                while (!pooled && attempts++ < 10)
                    pooled = getPooledConnection(emailCopy, "hydrate-user-fallback"_L1);
            }
        } else {
            // Semaphore ensures only 1 bg hydrate runs; one attempt is enough.
            pooled = getPooledConnection(emailCopy, "bg-hydrate"_L1);
        }

        const qint64 acquireMs = hydrateTimer.elapsed() - mapMs;

        if (!pooled) {
            if (userInitiated)
                emit hydrateStatus(false, "Message body fetch failed: IMAP connection pool timeout.");
            qWarning().noquote() << "[perf-hydrate]"
                                 << "uid=" << uidNorm
                                 << "mapMs=" << mapMs
                                 << "acquireMs=" << acquireMs
                                 << "result=pool-timeout";
            return {};
        }

        r.cxn = std::move(pooled);
        const QString html = Imap::MessageHydrator::execute(r);
        const qint64 totalMs = hydrateTimer.elapsed();
        const qint64 executeMs = totalMs - mapMs - acquireMs;
        qWarning().noquote() << "[perf-hydrate]"
                             << "uid=" << uidNorm
                             << "source=" << (userInitiated ? (usedDedicated ? "user-dedicated" : "user-pool") : "bg")
                             << "mapMs=" << mapMs
                             << "acquireMs=" << acquireMs
                             << "executeMs=" << executeMs
                             << "totalMs=" << totalMs
                             << "htmlLen=" << html.size();
        return html;
    }));
}

void
ImapService::moveMessage(const QString &accountEmail, const QString &folder,
                          const QString &uid, const QString &targetFolder) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty() || targetFolder.isEmpty())
        return;

    qint64 messageId = -1;
    int    unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

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

    if (m_offlineMode) {
        emit realtimeStatus(true, QStringLiteral("Offline mode: skipped IMAP move for %1.").arg(uid));
        return;
    }

    const auto retval = QtConcurrent::run([this, accountEmail, folder, uid, targetFolder, messageId, unreadVal]() {
        if (m_destroying.load()) return;

        auto cxn = getPooledConnection(accountEmail, "move-message");
        if (!cxn) return;

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(folder, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->select(folder);
            if (!ok) return;
        }

        const QString resp = cxn->execute("UID MOVE %1 \"%2\""_L1.arg(uid, targetFolder));

        qInfo().noquote() << "[move-message]"
                          << "uid=" << uid << "from=" << folder
                          << "to=" << targetFolder << "resp=" << resp.simplified().left(120);

        static const QRegularExpression kCopyUidRe(
            R"(\[COPYUID\s+\d+\s+\S+\s+(\d+)\])"_L1,
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = kCopyUidRe.match(resp);
        if (m.hasMatch() && messageId > 0) {
            const QString newUid = m.captured(1);
            QMetaObject::invokeMethod(this,
                [this, accountEmail, targetFolder, newUid, messageId, unreadVal]() {
                    if (!m_destroying.load() && m_store)
                        m_store->insertFolderEdge(accountEmail, messageId,
                                                  targetFolder, newUid, unreadVal);
                }, Qt::QueuedConnection);
        } else if (messageId > 0) {
            QMetaObject::invokeMethod(this,
                [this, accountEmail, messageId]() {
                    if (!m_destroying.load() && m_store)
                        m_store->removeAllEdgesForMessageId(accountEmail, messageId);
                }, Qt::QueuedConnection);
        }
    });
}

void
ImapService::markMessageRead(const QString &accountEmail, const QString &folder, const QString &uid) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty()) return;

    if (m_store)
        m_store->markMessageRead(accountEmail, uid);

    if (m_offlineMode)
        return;

    const auto retval = QtConcurrent::run([this, accountEmail, folder, uid]() {
        if (m_destroying.load()) return;

        auto cxn = getPooledConnection(accountEmail, "mark-read");
        if (!cxn) return;

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(folder, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->select(folder);
            if (!ok) return;
        }

        const QString result = cxn->execute(QStringLiteral("UID STORE %1 +FLAGS (\\Seen)").arg(uid));
        Q_UNUSED(result);
    });
}

void
ImapService::markMessageFlagged(const QString &accountEmail, const QString &folder, const QString &uid, bool flagged) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty()) return;

    if (m_store)
        m_store->markMessageFlagged(accountEmail, uid, flagged);

    if (m_offlineMode)
        return;

    const QString flags = flagged ? QStringLiteral("+FLAGS (\\Flagged)") : QStringLiteral("-FLAGS (\\Flagged)");
    const auto retval = QtConcurrent::run([this, accountEmail, folder, uid, flags]() {
        if (m_destroying.load()) return;

        auto cxn = getPooledConnection(accountEmail, "flag-message");
        if (!cxn) return;

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(folder, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->select(folder);
            if (!ok) return;
        }

        const QString result = cxn->execute(QStringLiteral("UID STORE %1 %2").arg(uid, flags));
        Q_UNUSED(result);
    });
    Q_UNUSED(retval);
}

void
ImapService::addMessageToFolder(const QString &accountEmail, const QString &folder,
                                const QString &uid, const QString &targetFolder) {
    if (m_destroying || accountEmail.isEmpty() || folder.isEmpty() || uid.isEmpty() || targetFolder.isEmpty())
        return;

    QString resolvedTarget = targetFolder.trimmed();
    if (m_store) {
        const QVariantList allFolders = m_store->folders();
        const QString desired = resolvedTarget.toLower();

        // Prefer canonical remote folder name/case from folders table.
        for (const QVariant &fv : allFolders) {
            const QVariantMap f = fv.toMap();
            const QString acc = f.value("accountEmail"_L1).toString().trimmed();
            if (acc.compare(accountEmail, Qt::CaseInsensitive) != 0) continue;

            const QString name = f.value("name"_L1).toString().trimmed();
            const QString special = f.value("specialUse"_L1).toString().trimmed().toLower();
            const QString lname = name.toLower();

            if (desired == QStringLiteral("important")) {
                if (special == QStringLiteral("important") || lname.endsWith(QStringLiteral("/important"))) {
                    resolvedTarget = name;
                    break;
                }
            } else if (lname == desired) {
                resolvedTarget = name;
                break;
            }
        }
    }

    if (resolvedTarget.compare(folder.trimmed(), Qt::CaseInsensitive) == 0)
        return;

    qint64 messageId = -1;
    int unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

        // Optimistic local membership so UI reflects the tag immediately.
        if (messageId > 0)
            m_store->insertFolderEdge(accountEmail, messageId, resolvedTarget, uid, unreadVal);
    }

    if (m_offlineMode) {
        emit realtimeStatus(true, QStringLiteral("Offline mode: skipped IMAP add-to-folder for %1.").arg(uid));
        return;
    }

    const auto retval = QtConcurrent::run([this, accountEmail, folder, uid, resolvedTarget, messageId, unreadVal]() {
        if (m_destroying.load()) return;

        auto cxn = getPooledConnection(accountEmail, "add-folder-membership");
        if (!cxn) return;

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(folder, Qt::CaseInsensitive) != 0) {
            const auto [ok, _] = cxn->select(folder);
            if (!ok) return;
        }

        const QString resp = cxn->execute(QStringLiteral("UID COPY %1 \"%2\"").arg(uid, resolvedTarget));

        static const QRegularExpression kCopyUidRe(
            R"(\[COPYUID\s+\d+\s+\S+\s+(\d+)\])"_L1,
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = kCopyUidRe.match(resp);

        const bool ok = resp.contains("OK"_L1, Qt::CaseInsensitive);
        if (!ok) {
            QMetaObject::invokeMethod(this, [this, accountEmail, resolvedTarget, uid]() {
                if (!m_destroying.load() && m_store)
                    m_store->deleteSingleFolderEdge(accountEmail, resolvedTarget, uid);
            }, Qt::QueuedConnection);
            return;
        }

        if (m.hasMatch() && messageId > 0) {
            const QString newUid = m.captured(1);
            QMetaObject::invokeMethod(this, [this, accountEmail, resolvedTarget, uid, newUid, messageId, unreadVal]() {
                if (m_destroying.load() || !m_store) return;
                if (newUid != uid)
                    m_store->deleteSingleFolderEdge(accountEmail, resolvedTarget, uid);
                m_store->insertFolderEdge(accountEmail, messageId, resolvedTarget, newUid, unreadVal);
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [this, resolvedTarget]() {
            if (!m_destroying.load())
                syncFolder(resolvedTarget, false);
        }, Qt::QueuedConnection);
    });
}

void
ImapService::removeMessageFromFolder(const QString &accountEmail, const QString &folder,
                                     const QString &uid, const QString &targetFolder) {
    if (m_destroying || accountEmail.isEmpty() || folder.isEmpty() || uid.isEmpty() || targetFolder.isEmpty())
        return;

    QString resolvedTarget = targetFolder.trimmed();
    if (m_store) {
        const QVariantList allFolders = m_store->folders();
        const QString desired = resolvedTarget.toLower();

        for (const QVariant &fv : allFolders) {
            const QVariantMap f = fv.toMap();
            const QString acc = f.value("accountEmail"_L1).toString().trimmed();
            if (acc.compare(accountEmail, Qt::CaseInsensitive) != 0)
                continue;

            const QString name = f.value("name"_L1).toString().trimmed();
            const QString special = f.value("specialUse"_L1).toString().trimmed().toLower();
            const QString lname = name.toLower();

            if (desired == QStringLiteral("important")) {
                if (special == QStringLiteral("important") || lname.endsWith(QStringLiteral("/important"))) {
                    resolvedTarget = name;
                    break;
                }
            } else if (lname == desired) {
                resolvedTarget = name;
                break;
            }
        }
    }

    // Look up message_id and the message's UID in the target folder before the local deletion.
    // Gmail labels are IMAP folders — removing the label means selecting the label folder,
    // marking that UID \Deleted, and expunging. This mirrors what addMessageToFolder does
    // with UID COPY and requires no Gmail-specific extensions.
    qint64 messageId = -1;
    QString targetUid;
    if (m_store) {
        const QVariantMap srcEdge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = srcEdge.value("messageId"_L1, -1LL).toLongLong();
        if (messageId > 0)
            targetUid = m_store->folderUidForMessageId(accountEmail, resolvedTarget, messageId);
    }

    // Optimistic local removal.
    if (m_store) {
        if (messageId > 0)
            m_store->deleteFolderEdgesForMessage(accountEmail, resolvedTarget, messageId);
        else
            m_store->deleteSingleFolderEdge(accountEmail, resolvedTarget, uid);
    }

    if (m_offlineMode) {
        emit realtimeStatus(true, QStringLiteral("Offline mode: skipped IMAP remove-from-folder for %1.").arg(uid));
        return;
    }

    const auto retval = QtConcurrent::run([this, accountEmail, folder, uid, resolvedTarget, targetUid]() {
        if (m_destroying.load())
            return;

        auto cxn = getPooledConnection(accountEmail, "remove-folder-membership");
        if (!cxn)
            return;

        if (!targetUid.isEmpty()) {
            // Standard IMAP folder removal: select the label folder, mark deleted, expunge.
            // On Gmail this removes the label without permanently deleting the message
            // (it remains in [Gmail]/All Mail and any other labeled folders).
            const auto [ok, _] = cxn->select(resolvedTarget);
            if (ok) {
                (void)cxn->execute(QStringLiteral("UID STORE %1 +FLAGS (\\Deleted)").arg(targetUid));
                (void)cxn->execute(QStringLiteral("UID EXPUNGE %1").arg(targetUid));
            }
        }
        // If we have no targetUid the label folder hasn't been synced yet; the optimistic
        // local deletion stands and the next folder sync will reconcile server state.

        QMetaObject::invokeMethod(this, [this, resolvedTarget]() {
            if (!m_destroying.load())
                syncFolder(resolvedTarget, false);
        }, Qt::QueuedConnection);
    });
}

void
ImapService::syncFolder(const QString &folderName, bool announce) {
    if (m_destroying)
        return;

    const auto target = folderName.trimmed().isEmpty() ? "INBOX"_L1 : folderName.trimmed();

    if (m_offlineMode) {
        if (announce)
            emit syncFinished(true, QStringLiteral("Offline mode: skipped sync for %1.").arg(target));
        return;
    }
    const QString syncTargetKey = target.trimmed().toLower();

    if (!announce) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QMutexLocker lock(&m_lastFolderSyncStartMsMutex);
        const qint64 lastMs = m_lastFolderSyncStartMs.value(syncTargetKey, 0);
        static constexpr qint64 kMinIntervalMs = 30000; // coalesce noisy background re-syncs
        if (lastMs > 0 && (nowMs - lastMs) < kMinIntervalMs)
            return;
        m_lastFolderSyncStartMs.insert(syncTargetKey, nowMs);
    }

    {
        QMutexLocker lock(&m_activeFolderSyncTargetsMutex);
        if (m_activeFolderSyncTargets.contains(syncTargetKey))
            return;
        m_activeFolderSyncTargets.insert(syncTargetKey);
    }
    const auto releaseSyncTarget = [this, syncTargetKey]() {
        QMutexLocker lock(&m_activeFolderSyncTargetsMutex);
        m_activeFolderSyncTargets.remove(syncTargetKey);
    };

    if (target.contains("/Categories/"_L1, Qt::CaseInsensitive)) {
        if (announce)
            emit syncFinished(true, "Category folders are managed via labels.");
        releaseSyncTarget();
        return;
    }


    if (!m_accounts || !m_store) {
        if (announce)
            emit syncFinished(false, "Sync dependencies not initialized.");
        releaseSyncTarget();
        return;
    }

    const auto accounts = m_accounts->accounts();
    if (accounts.isEmpty()) {
        if (announce)
            emit syncFinished(false, "No accounts configured yet.");
        releaseSyncTarget();
        return;
    }

    runAsync(
        [this, accounts, target, announce]() -> SyncResult {
            // Cap concurrent background syncs to keep pool connections available.
            const bool isBgSync = !announce;
            if (isBgSync && !g_bgFolderSyncSem.tryAcquire(1, 30'000))
                return SyncResult{};
            struct FolderSyncGuard {
                bool active;
                ~FolderSyncGuard() { if (active) g_bgFolderSyncSem.release(); }
            } fsgGuard{isBgSync};

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

                // Write headers directly from the worker thread (DataStore is thread-safe).
                static constexpr int kDbChunk = 5;
                for (int start = 0; start < batch.size(); start += kDbChunk) {
                    const QVariantList chunk = batch.mid(start, kDbChunk);
                    if (m_store) m_store->upsertHeaders(chunk);
                }

                // Body-fetch dispatch posted to UI thread (backgroundFetchBodies needs it).
                QMetaObject::invokeMethod(this, [this, batch]() {
                    if (!m_store) return;
                    QSet<QString> dispatched;
                    for (const QVariant &hv : batch) {
                        const QVariantMap h = hv.toMap();
                        const QString email = h.value("accountEmail"_L1).toString().trimmed();
                        const QString folder = h.value("folder"_L1).toString().trimmed();
                        if (email.isEmpty() || folder.isEmpty()) continue;
                        const QString key = email.toLower() + "|"_L1 + folder.toLower();
                        if (dispatched.contains(key)) continue;
                        dispatched.insert(key);
                        backgroundFetchBodies({}, email, folder, {});
                    }
                }, Qt::QueuedConnection);

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

            // Final flush handles any remaining headers and dispatches backgroundFetchBodies.
            flush();
            result.ok      = true;
            result.inserted = inboxInserted;
            result.message  = QStringLiteral("%1 sync complete.").arg(target);

            return result;
        },
        [this, announce, target, releaseSyncTarget](const SyncResult &r) {
            QString folderLabel = target;
            if (folderLabel.startsWith("[Google Mail]/"_L1, Qt::CaseInsensitive))
                folderLabel = folderLabel.mid(QStringLiteral("[Google Mail]/").size());
            if (folderLabel.startsWith("[Gmail]/"_L1, Qt::CaseInsensitive))
                folderLabel = folderLabel.mid(QStringLiteral("[Gmail]/").size());
            if (folderLabel.compare("INBOX"_L1, Qt::CaseInsensitive) == 0)
                folderLabel = "Inbox"_L1;

            QSet<QString> uniqueUids;
            uniqueUids.reserve(r.headers.size());
            for (const QVariant &hv : r.headers) {
                const QString uid = hv.toMap().value("uid"_L1).toString();
                if (!uid.isEmpty())
                    uniqueUids.insert(uid);
            }
            const int syncedCount = uniqueUids.isEmpty() ? r.headers.size() : uniqueUids.size();

            if (announce) {
                if (!r.ok)
                    emit syncFinished(false, r.message);
                else if (syncedCount > 0)
                    emit syncFinished(true, QStringLiteral("%1 synced %2 messages.").arg(folderLabel).arg(syncedCount));
                releaseSyncTarget();
                return;
            }

            if (!r.ok) {
                emit realtimeStatus(false, r.message);
            } else if (syncedCount > 0) {
                emit realtimeStatus(true, QStringLiteral("%1 synced %2 messages.").arg(folderLabel).arg(syncedCount));
            }
            releaseSyncTarget();
        }
    );
}

void
ImapService::syncAll(bool announce) {
    if (m_destroying)
        return;

    if (m_offlineMode) {
        if (announce)
            emit syncFinished(true, QStringLiteral("Offline mode: skipped sync all."));
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

                static constexpr int kDbChunk = 5;
                for (int start = 0; start < batch.size(); start += kDbChunk) {
                    const QVariantList chunk = batch.mid(start, kDbChunk);
                    if (m_store) m_store->upsertHeaders(chunk);
                }

                QMetaObject::invokeMethod(this, [this, batch]() {
                    if (!m_store) return;
                    QSet<QString> dispatched;
                    for (const QVariant &hv : batch) {
                        const QVariantMap h = hv.toMap();
                        const QString email = h.value("accountEmail"_L1).toString().trimmed();
                        const QString folder = h.value("folder"_L1).toString().trimmed();
                        if (email.isEmpty() || folder.isEmpty()) continue;
                        const QString key = email.toLower() + "|"_L1 + folder.toLower();
                        if (dispatched.contains(key)) continue;
                        dispatched.insert(key);
                        backgroundFetchBodies({}, email, folder, {});
                    }
                }, Qt::QueuedConnection);

                flushTimer.restart();
            };

            QString googleAccessToken;
            for (const auto &[email, host, accessToken, port] : resolveAccounts(accounts)) {
                if (googleAccessToken.isEmpty()
                        && (host.contains("gmail.com"_L1) || host.contains("google"_L1)))
                    googleAccessToken = accessToken;

                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Refresh aborted.";
                    return r;
                }

                // Fetch and store folder list
                auto pooled = getPooledConnection(email, "sync-all-folders");
                if (!pooled) continue;
                QString folderStatus;
                const auto folders = Imap::SyncEngine::fetchFolders(pooled, &folderStatus, true);
                pooled.reset();
                for (const auto &fv : folders) {
                    if (m_store) m_store->upsertFolder(fv.toMap());
                }

                // Build ordered sync target list (INBOX first, then all non-category folders)
                auto canonicalFolderKey = [](const QString &folderName) {
                    QString k = folderName.trimmed().toLower();
                    if (k.startsWith("[gmail]/"_L1))
                        k = k.mid(QStringLiteral("[gmail]/").size());
                    else if (k.startsWith("[google mail]/"_L1))
                        k = k.mid(QStringLiteral("[google mail]/").size());
                    if (k == "inbox"_L1)
                        return QStringLiteral("inbox");
                    return k;
                };

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
                    const auto canonName = canonicalFolderKey(name);

                    const bool isCategory = name.contains("/Categories/"_L1, Qt::CaseInsensitive);
                    const bool isContainerRoot = name.compare("[Gmail]"_L1, Qt::CaseInsensitive) == 0
                                              || name.compare("[Google Mail]"_L1, Qt::CaseInsensitive) == 0;
                    const bool isNoSelect = flags.contains("\\noselect"_L1);
                    const bool isParentContainer = parentContainers.contains(lowerName);

                    if (name.isEmpty() || isCategory || isContainerRoot || isNoSelect || isParentContainer)
                        continue;

                    if (!seen.contains(canonName)) {
                        seen.insert(canonName); targets << name;
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

            // Re-fetch Google People contact photos that are stale or missing.
            // Runs after the main sync so the access token is known and valid.
            if (!m_cancelRequested.load() && !googleAccessToken.isEmpty()) {
                QStringList staleEmails;
                if (m_store)
                    staleEmails = m_store->staleGooglePeopleEmails();

                for (const QString &sEmail : staleEmails) {
                    if (m_cancelRequested.load()) break;
                    const QString url = resolveGooglePeopleAvatarUrl(sEmail, googleAccessToken);
                    if (url.isEmpty()) continue;
                    const QString blob = fetchAvatarBlob(url, googleAccessToken);
                    if (!blob.startsWith("data:"_L1)) continue;
                    const QString fileUrl = writeAvatarFile(sEmail, blob);
                    if (fileUrl.isEmpty()) continue;
                    if (m_store) m_store->updateContactAvatar(sEmail, fileUrl, "google-people"_L1);
                }
            }

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
