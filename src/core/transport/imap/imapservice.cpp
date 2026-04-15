#include "imapservice.h"

#include "../../auth/oauthservice.h"
#include "../../auth/tokenvault.h"
#include "../../store/datastore.h"
#include "../../utils.h"
#include "connection/imapconnection.h"
#include "connection/connectionpool.h"
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
#include <algorithm>
#include <ranges>

#include "sync/idlewatcher.h"
#include "sync/kestreltimer.h"
#include "message/avatarresolver.h"

using namespace Qt::Literals::StringLiterals;
using Imap::AvatarResolver::resolveGooglePeopleAvatarUrl;
using Imap::AvatarResolver::fetchAvatarBlob;
using Imap::AvatarResolver::writeAvatarFile;

namespace {

const QRegularExpression kCopyUidRe(
    R"(\[COPYUID\s+\d+\s+\S+\s+(\d+)\])"_L1,
    QRegularExpression::CaseInsensitiveOption);

bool offlineModeEnabledFromEnv() {
    const QByteArray raw = qgetenv("KESTREL_OFFLINE_MODE").trimmed().toLower();
    if (raw.isEmpty())
        return false;
    return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
}

// Strip "[Gmail]/" or "[Google Mail]/" prefix and normalise "INBOX" → "Inbox".
QString stripGmailPrefix(const QString &folder) {
    auto label = folder;
    if (label.startsWith("[Google Mail]/"_L1, Qt::CaseInsensitive)) {
        label = label.mid(14); // strlen("[Google Mail]/")
    } else if (label.startsWith("[Gmail]/"_L1, Qt::CaseInsensitive)) {
        label = label.mid(8); // strlen("[Gmail]/")
    }
    if (label.compare("INBOX"_L1, Qt::CaseInsensitive) == 0) {
        label = "Inbox"_L1;
    }
    return label;
}
}



ImapService::ImapService(const QVariantMap &accountConfig, DataStore *store, TokenVault *vault,
                         QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_vault(vault)
    , m_accountConfig(accountConfig)
    , m_email(accountConfig.value("email"_L1).toString().trimmed().toLower())
    , m_host(accountConfig.value("imapHost"_L1).toString())
    , m_port(accountConfig.value("imapPort"_L1).toInt())
    , m_authMethod(accountConfig.value("authType"_L1).toString() == "password"_L1
                       ? Imap::AuthMethod::Login : Imap::AuthMethod::XOAuth2)
    , m_offlineMode(offlineModeEnabledFromEnv()) {
    // Per-account constructor — pool created and initialized for this single account.
    m_pool = std::make_unique<Imap::ConnectionPool>();
    m_pool->setTokenRefresher([this](const QString &e) -> QString {
        if (m_authMethod == Imap::AuthMethod::Login)
            return m_vault ? m_vault->loadPassword(e) : QString{};
        return refreshAccessToken(m_accountConfig, e);
    });
    if (!m_offlineMode) {
        const QString credential = (m_authMethod == Imap::AuthMethod::Login && m_vault)
            ? m_vault->loadPassword(m_email)
            : QString{};
        m_pool->initialize(m_email, m_host, m_port, m_authMethod, credential);
    }
    m_expectedPoolSize = m_pool->expectedConnections();
}

ImapService::ImapService(DataStore *store, TokenVault *vault, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_vault(vault)
    , m_offlineMode(offlineModeEnabledFromEnv()) {
    if (m_offlineMode)
        qInfo() << "[offline-mode] KESTREL_OFFLINE_MODE enabled; IMAP network operations are disabled.";
    Imap::Connection::setThrottleObserver([this](const QString &accountEmail, bool throttled, const QString &response) {
        const QString msg = "Account throttled by server: %1"_L1
                                .arg(response.simplified().left(240));
        QMetaObject::invokeMethod(this, [this, accountEmail, throttled, msg]() {
            if (m_destroying.load()) return;

            const QString key = Kestrel::normalizeEmail(accountEmail);
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
ImapService::initializeConnectionPool() {
    // Per-account mode: pool already initialized in the constructor.
    if (isPerAccountMode())
        return;

    // Global mode: create an empty fallback pool. Per-account pools are
    // registered by AccountManager and handle all real connections.
    if (!m_pool)
        m_pool = std::make_unique<Imap::ConnectionPool>();
}

qint32
ImapService::expectedPoolConnections() const {
    return m_expectedPoolSize;
}

qint32
ImapService::poolConnectionsReady() const {
    qint32 total = m_pool ? m_pool->readyConnections() : 0;
    for (auto *pool : m_accountPools)
        total += pool->readyConnections();
    return total;
}

void
ImapService::registerAccount(const QString &email, const QVariantMap &config,
                             Imap::ConnectionPool *pool) {
    const auto key = email.trimmed().toLower();
    m_accountConfigs.insert(key, config);
    if (pool) {
        m_accountPools.insert(key, pool);
        m_expectedPoolSize += pool->expectedConnections();
    }
}

void
ImapService::unregisterAccount(const QString &email) {
    const auto key = email.trimmed().toLower();
    m_accountConfigs.remove(key);
    if (auto *pool = m_accountPools.value(key)) {
        m_expectedPoolSize -= pool->expectedConnections();
        m_accountPools.remove(key);
    }
}

Imap::ConnectionPool*
ImapService::poolForEmail(const QString &email) const {
    if (isPerAccountMode())
        return m_pool.get();
    const auto key = email.trimmed().toLower();
    if (auto *pool = m_accountPools.value(key))
        return pool;
    return m_pool.get();
}

void
ImapService::initialize() {
    initializeConnectionPool();

    if (m_offlineMode) {
        emit realtimeStatus(true, "Offline mode is enabled (KESTREL_OFFLINE_MODE=1). IMAP sync is paused."_L1);
        return;
    }

    // Idle watchers and background workers are now owned by individual account
    // objects (IAccount::initialize). ImapService only manages the connection pool.
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

        const bool anyRunning = std::ranges::any_of(snapshot, [](const QFutureWatcherBase *w) {
            return w && w->isRunning();
        });

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

    if (m_pool)
        m_pool->shutdown();
    for (auto *pool : m_accountPools)
        pool->shutdown();

    // Account-owned workers are shut down by AccountManager.

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
        defaultName = "attachment"_L1;

    const QString outPath = QFileDialog::getSaveFileName(nullptr, "Save Attachment"_L1, defaultName);
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

    const qint32 comma = static_cast<qint32>(src.indexOf(','));
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

// Category folders (e.g. [Gmail]/Categories/Primary) are synthetic labels —
// the underlying messages live in INBOX.  Map to INBOX when selecting.
static QString
resolveSourceFolder(const QString &folder) {
    if (folder.contains("/Categories/"_L1, Qt::CaseInsensitive)) {
        return "INBOX"_L1;
    }
    return folder;
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
    prefetchAttachmentsInternal(accountEmail, folderName, uid, false);
}

void
ImapService::prefetchImageAttachments(const QString &accountEmail, const QString &folderName, const QString &uid) {
    prefetchAttachmentsInternal(accountEmail, folderName, uid, true);
}

void
ImapService::prefetchAttachmentsInternal(const QString &accountEmail, const QString &folderName,
                                         const QString &uid, const bool imagesOnly) {
    if (m_offlineMode || !m_store) {
        return;
    }

    const auto attachments = m_store->attachmentsForMessage(accountEmail, folderName, uid);
    if (attachments.isEmpty()) {
        return;
    }

    QVariantList toFetch;
    std::ranges::copy_if(attachments, std::back_inserter(toFetch), [&](const QVariant &a) {
        const auto am = a.toMap();
        if (imagesOnly && !isImageAttachment(am)) {
            return false;
        }
        return cachedAttachmentPath(accountEmail, uid, am.value("partId"_L1).toString()).isEmpty();
    });
    if (toFetch.isEmpty()) {
        return;
    }

    // Dedup by (account, sorted-partIds): all folder representations of the same
    // message have identical attachment parts.  Only one background task needs to
    // run per message -- prevents N concurrent pool acquisitions when QML fires
    // one prefetchAttachments call per candidate folder.
    QStringList partIds;
    std::ranges::transform(toFetch, std::back_inserter(partIds),
                           [](const QVariant &a) { return a.toMap().value("partId"_L1).toString(); });
    std::ranges::sort(partIds);
    const auto taskKey = "task:"_L1 + accountEmail + "|"_L1 + partIds.join(","_L1);

    {
        QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
        if (m_inFlightAttachmentDownloads.contains(taskKey)) {
            return;
        }
        m_inFlightAttachmentDownloads.insert(taskKey);
    }

    const auto label = imagesOnly ? "prefetch-images" : "prefetch-attachments";
    runBackgroundTask([this, accountEmail, folderName, uid, toFetch, taskKey, label]() {
        struct TaskGuard {
            ImapService *svc;
            QString key;
            ~TaskGuard() {
                QMutexLocker lock(&svc->m_inFlightAttachmentDownloadsMutex);
                svc->m_inFlightAttachmentDownloads.remove(key);
            }
        } taskGuard{this, taskKey};

        auto cxn = poolForEmail(accountEmail)->acquire(label, accountEmail);
        if (!cxn) {
            qWarning() << "[imap-pool] timed out acquiring operational connection for" << accountEmail;
            return;
        }
        if (cxn->selectedFolder().compare(folderName, Qt::CaseInsensitive) != 0) {
            if (!cxn->examine(folderName)) {
                return;
            }
        }

        const auto cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + "/attachments/"_L1 + accountEmail + "/"_L1 + uid;

        for (const auto &a : toFetch) {
            if (m_destroying.load()) {
                break;
            }

            const auto    am       = a.toMap();
            const QString partId   = am.value("partId"_L1).toString();
            const QString encoding = am.value("encoding"_L1).toString();
            const QString name     = am.value("name"_L1).toString();

            const auto key = attachmentCacheKey(accountEmail, uid, partId);

            {
                QMutexLocker lock(&m_inFlightAttachmentDownloadsMutex);
                if (m_inFlightAttachmentDownloads.contains(key)) {
                    continue;
                }
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
            const auto data = fetchAttachmentPartById(*cxn, uid, partId, encoding,
                [this, &accountEmail, &uid, &partId](const int percent, const qint64) {
                    emit attachmentDownloadProgress(accountEmail, uid, partId, percent);
                });
            if (data.isEmpty()) {
                emit attachmentDownloadProgress(accountEmail, uid, partId, 0);
                clearInFlight();
                continue;
            }

            const auto safePartId = QString(partId).replace('/', '_').replace('.', '_');
            const auto partDir    = cacheBase + "/"_L1 + safePartId;
            QDir().mkpath(partDir);

            const auto safeName  = QString(name).replace(QRegularExpression("[^A-Za-z0-9._-]"_L1), "_"_L1);
            const auto localPath = partDir + "/"_L1 + (safeName.isEmpty() ? "attachment"_L1 : safeName);

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

    emit realtimeStatus(false, "Attachment is still downloading. Please try again shortly."_L1);
}

bool
ImapService::saveAttachment(const QString &accountEmail, const QString &, const QString &uid,
                            const QString &partId, const QString &fileName, const QString &) {
    const QString cached = cachedAttachmentPath(accountEmail, uid, partId);
    if (cached.isEmpty() || !QFile::exists(cached)) {
        emit realtimeStatus(false, "Attachment is still downloading. Please try again shortly."_L1);
        return false;
    }

    QFile in(cached);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = in.readAll();
    in.close();

    if (data.isEmpty())
        return false;

    const QString outPath = QFileDialog::getSaveFileName(nullptr, "Save Attachment"_L1,
                                                         fileName.isEmpty() ? "attachment"_L1 : fileName);
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

    if (pendingFull) {
        // Full sync is now owned by account objects — nothing to drain here.
        Q_UNUSED(pendingAnnounce)
    } else if (!pending.isEmpty())
        QMetaObject::invokeMethod(this, [this, pending, pendingAnnounce]() { syncFolder(pending, pendingAnnounce); },
                                  Qt::QueuedConnection);
}

void
ImapService::runAsync(std::function<SyncResult()> work, std::function<void(const SyncResult&)> onDone) {
    m_cancelRequested.store(false);
    const qint32 prev = m_syncInProgress.fetch_add(1);
    if (prev == 0)
        emit syncActivityChanged(true);

    auto *watcher = new QFutureWatcher<SyncResult>(this);
    registerWatcher(watcher);
    connect(watcher, &QFutureWatcher<SyncResult>::finished, this,
            [this, watcher, onDone = std::move(onDone)]() {
        const auto r = watcher->result();

        if (!m_destroying && onDone)
            onDone(r);

        const qint32 now = m_syncInProgress.fetch_sub(1) - 1;
        if (now <= 0)
            emit syncActivityChanged(false);

        unregisterWatcher(watcher);
        watcher->deleteLater();

    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}





QVariantList
ImapService::workerGetAccounts() const {
    return accountConfigList();
}

QString
ImapService::workerRefreshAccessToken(const QVariantMap &account, const QString &email) {
    return refreshAccessToken(account, email);
}


void
ImapService::workerEmitRealtimeStatus(const bool ok, const QString &message) {
    auto fn = [this, ok, message]() {
        // Auto-trigger reauth flow when the account is missing or broken,
        // suppressing the raw error toast in favour of the reauth prompt.
        if (!ok && (message.contains("no OAuth account"_L1, Qt::CaseInsensitive)
                 || message.contains("account settings incomplete"_L1, Qt::CaseInsensitive))) {
            const auto configs = accountConfigList();
            const auto email = isPerAccountMode() ? m_email
                : (!configs.isEmpty()
                    ? configs.first().toMap().value("email"_L1).toString()
                    : QString{});
            if (!email.isEmpty()) {
                emit accountNeedsReauth(email);
                return;
            }
        }

        emit realtimeStatus(ok, message);
    };

    if (QThread::currentThread() == thread())
        fn();
    else
        QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

void
ImapService::wireIdleWatcher(Imap::IdleWatcher *watcher, const QString &accountEmail) {
    if (!watcher) return;

    connect(watcher, &Imap::IdleWatcher::requestAccounts,
            this, [this](QVariantList *out) {
                if (out) *out = workerGetAccounts();
            }, Qt::BlockingQueuedConnection);

    connect(watcher, &Imap::IdleWatcher::requestRefreshAccessToken,
            this, [this](const QVariantMap &account, const QString &email, QString *out) {
                if (!out) return;
                if (account.value("authType"_L1).toString() == "password"_L1)
                    *out = m_vault ? m_vault->loadPassword(email) : QString{};
                else
                    *out = workerRefreshAccessToken(account, email);
            }, Qt::BlockingQueuedConnection);

    connect(watcher, &Imap::IdleWatcher::requestFolderUids,
            this, [this](const QString &email, const QString &folder, QStringList *out) {
                if (out && m_store) *out = m_store->folderUids(email, folder);
            }, Qt::DirectConnection);

    connect(watcher, &Imap::IdleWatcher::pruneFolderToUidsRequested,
            this, [this](const QString &email, const QString &folder, const QStringList &uids) {
                if (m_store) m_store->pruneFolderToUids(email, folder, uids);
            }, Qt::DirectConnection);

    connect(watcher, &Imap::IdleWatcher::removeUidsRequested,
            this, [this](const QString &email, const QStringList &uids) {
                if (m_store) m_store->removeAccountUidsEverywhere(email, uids);
            }, Qt::DirectConnection);

    connect(watcher, &Imap::IdleWatcher::realtimeStatus,
            this, [this](const bool ok, const QString &message) {
                workerEmitRealtimeStatus(ok, message);
            }, Qt::QueuedConnection);

    Q_UNUSED(accountEmail)
}

void
ImapService::wireBackgroundWorker(Imap::BackgroundWorker *worker, const QString &accountEmail) {
    if (!worker) return;

    connect(worker, &Imap::BackgroundWorker::requestAccounts,
            this, [this](QVariantList *out) {
                if (out) *out = workerGetAccounts();
            }, Qt::BlockingQueuedConnection);

    connect(worker, &Imap::BackgroundWorker::requestRefreshAccessToken,
            this, [this](const QVariantMap &account, const QString &email, QString *out) {
                if (!out) return;
                if (account.value("authType"_L1).toString() == "password"_L1)
                    *out = m_vault ? m_vault->loadPassword(email) : QString{};
                else
                    *out = workerRefreshAccessToken(account, email);
            }, Qt::BlockingQueuedConnection);

    connect(worker, &Imap::BackgroundWorker::requestAccountThrottled,
            this, [this](const QString &email, bool *out) {
                if (out) *out = m_accountThrottleState.value(Kestrel::normalizeEmail(email), false);
            }, Qt::BlockingQueuedConnection);

    connect(worker, &Imap::BackgroundWorker::upsertFoldersRequested,
            this, [this](const QVariantList &folders) {
                for (const auto &fv : folders)
                    if (m_store) m_store->upsertFolder(fv.toMap());
            }, Qt::DirectConnection);

    connect(worker, &Imap::BackgroundWorker::loadFolderStatusSnapshotRequested,
            this, [this](const QString &email, const QString &folder,
                         qint64 *uidNext, qint64 *highestModSeq, qint64 *messages, bool *found) {
                const auto snap = loadFolderStatusSnapshot(email, folder);
                if (found) *found = !snap.isEmpty();
                if (uidNext) *uidNext = snap.value("uidNext"_L1).toLongLong();
                if (highestModSeq) *highestModSeq = snap.value("highestModSeq"_L1).toLongLong();
                if (messages) *messages = snap.value("messages"_L1).toLongLong();
            }, Qt::DirectConnection);

    connect(worker, &Imap::BackgroundWorker::saveFolderStatusSnapshotRequested,
            this, [this](const QString &email, const QString &folder,
                         qint64 uidNext, qint64 highestModSeq, qint64 messages) {
                saveFolderStatusSnapshot(email, folder, uidNext, highestModSeq, messages);
            }, Qt::DirectConnection);

    connect(worker, &Imap::BackgroundWorker::idleLiveUpdateRequested,
            this, [this](const QVariantMap &, const QString &email) {
                backgroundOnIdleLiveUpdate({}, email);
            }, Qt::QueuedConnection);

    connect(worker, &Imap::BackgroundWorker::loopError,
            this, [this](const QString &message) {
                workerEmitRealtimeStatus(false, message);
            }, Qt::QueuedConnection);

    connect(worker, &Imap::BackgroundWorker::realtimeStatus,
            this, [this](const bool ok, const QString &message) {
                workerEmitRealtimeStatus(ok, message);
            }, Qt::QueuedConnection);

    Q_UNUSED(accountEmail)
}




void
ImapService::backgroundFetchBodies(const QVariantMap &, const QString &email, const QString &folder,
                                   const QString &) {
    if (!m_store) {
        return;
    }

    const QString folderNorm = folder.trimmed();
    if (folderNorm.isEmpty()) {
        return;
    }

    const auto folderLower = folderNorm.toLower();
    const bool isInboxish = (folderLower == "inbox"_L1
                             || folderLower == "[gmail]/inbox"_L1
                             || folderLower == "[google mail]/inbox"_L1
                             || folderLower.endsWith("/inbox"_L1));
    // Product policy: background body hydration only for Inbox-class folders.
    if (!isInboxish) {
        return;
    }

    // Throttle gate — rate-limit the skip log to once per minute per account.
    if (m_accountThrottleState.value(Kestrel::normalizeEmail(email), false)) {
        static QHash<QString, qint64> s_lastThrottleSkipLogMs;
        const auto nowMs = QDateTime::currentMSecsSinceEpoch();
        const auto k = Kestrel::normalizeEmail(email);
        if (!s_lastThrottleSkipLogMs.contains(k) || (nowMs - s_lastThrottleSkipLogMs.value(k)) > 60000) {
            s_lastThrottleSkipLogMs.insert(k, nowMs);
            qInfo().noquote() << "[bg-hydrate]" << "account=" << email << "paused=true reason=throttled";
        }
        return;
    }

    // Dedup — only one background hydration per account+folder at a time.
    const auto key = Kestrel::normalizeEmail(email) + "|"_L1 + folderLower;
    {
        QMutexLocker lock(&m_bgHydrateMutex);
        if (m_activeBgHydrateFolders.contains(key)) {
            m_pendingBgHydrateFolders.insert(key);
            return;
        }
        m_activeBgHydrateFolders.insert(key);
    }

    bool ok = false;
    const qint32 configuredLimit = qEnvironmentVariableIntValue("KESTREL_BG_BODY_FETCH_LIMIT", &ok);
    const qint32 limit = ok && configuredLimit > 0 ? configuredLimit : 8;

    runBackgroundTask([this, email, folderNorm, key, limit]() {
        hydrateFolderBodies(email, folderNorm, key, limit);
    });
}

void
ImapService::hydrateFolderBodies(const QString &email, const QString &folder,
                                 const QString &key, const qint32 limit) {
    qint32 noProgressPasses = 0;
    qint32 processedThisRun = 0;

    bool chunkOk = false;
    const qint32 configuredChunk = qEnvironmentVariableIntValue("KESTREL_BG_HYDRATE_PER_LOOP", &chunkOk);
    const qint32 maxPerLoop = chunkOk ? qBound(25, configuredChunk, 50) : 40;

    while (!m_destroying.load() && processedThisRun < maxPerLoop) {
        QStringList candidates;
        if (m_store) {
            candidates = m_store->bodyFetchCandidates(email, folder, limit);
        }

        if (candidates.isEmpty()) {
            break;
        }

        qint32 hydratedThisPass = 0;
        for (const auto &uid : candidates) {
            if (m_destroying.load() || processedThisRun >= maxPerLoop) {
                break;
            }

            const auto inFlightKey = email.trimmed() + "|"_L1 + folder.toLower() + "|"_L1 + uid.trimmed();

            QMetaObject::invokeMethod(this,
                                      [this, email, folder, uid]() {
                                          hydrateMessageBodyInternal(email, folder, uid, false);
                                      },
                                      Qt::QueuedConnection);

            // Pace hydration and keep exactly one background fetch active at a time.
            // Wait for this UID's in-flight marker to clear before moving on.
            const auto waitStartMs = QDateTime::currentMSecsSinceEpoch();
            while (!m_destroying.load()) {
                bool stillInFlight = false;
                {
                    QMutexLocker inFlightLock(&m_inFlightBodyHydrationsMutex);
                    stillInFlight = m_inFlightBodyHydrations.contains(inFlightKey);
                }
                if (!stillInFlight) {
                    break;
                }
                if ((QDateTime::currentMSecsSinceEpoch() - waitStartMs) > 90'000) {
                    break;
                }
                QThread::msleep(120);
            }

            ++processedThisRun;

            if (m_store) {
                const auto row = m_store->messageByKey(email, folder, uid);
                const auto body = row.value("bodyHtml"_L1).toString();
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
                                  << "folder=" << folder
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
                          << "folder=" << folder
                          << "processed=" << processedThisRun
                          << "maxPerLoop=" << maxPerLoop;
    }

    QMutexLocker lock(&m_bgHydrateMutex);
    m_activeBgHydrateFolders.remove(key);
    m_pendingBgHydrateFolders.remove(key);
}


void
ImapService::backgroundOnIdleLiveUpdate(const QVariantMap &, const QString &email) {
    auto fn = [this, email]() {
        if (m_destroying)
            return;

        // Keep background hydration progressing even when folder STATUS values are stable
        // and no header sync is dispatched in this loop. Inbox-only policy is enforced
        // inside backgroundFetchBodies().
        if (!email.trimmed().isEmpty())
            backgroundFetchBodies({}, email, "INBOX"_L1, {});
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
    if (!m_vault) {
        return {};
    }

    const auto refreshToken = m_vault->loadRefreshToken(email);
    if (refreshToken.isEmpty()) {
        qWarning() << "[token-refresh] No stored refresh token for" << email;
        if (m_idleWatcher) {
            m_idleWatcher->authSuspended.store(true);
        }
        QMetaObject::invokeMethod(this, [this, email]() {
            emit accountNeedsReauth(email);
        }, Qt::QueuedConnection);
        return {};
    }

    const auto tokenUrl      = account.value("oauthTokenUrl"_L1).toString();
    const auto clientId      = account.value("oauthClientId"_L1).toString().trimmed();
    const auto clientSecret  = account.value("oauthClientSecret"_L1).toString();

    const auto result = OAuthService::refreshAccessToken(tokenUrl, refreshToken,
                                                         clientId, clientSecret);

    if (result.accessToken.isEmpty() && result.invalidGrant) {
        qWarning() << "[token-refresh] Refresh token revoked for" << email;
        if (m_idleWatcher) {
            m_idleWatcher->authSuspended.store(true);
        }
        QMetaObject::invokeMethod(this, [this, email]() {
            emit accountNeedsReauth(email);
        }, Qt::QueuedConnection);
    }

    return result.accessToken;
}

QVariantList
ImapService::accountConfigList() const {
    if (isPerAccountMode())
        return { m_accountConfig };
    QVariantList result;
    result.reserve(m_accountConfigs.size());
    for (const auto &config : m_accountConfigs)
        result.push_back(config);
    return result;
}

QList<ImapService::AccountInfo>
ImapService::resolveAccounts(const QVariantList &accounts) {
    QList<AccountInfo> result;
    for (const auto &a : accounts) {
        const auto acc = a.toMap();

        const auto authType = acc.value("authType"_L1).toString();
        const bool isOAuth = authType == "oauth2"_L1
                          || (!acc.value("oauthClientId"_L1).toString().isEmpty()
                           && !acc.value("oauthTokenUrl"_L1).toString().isEmpty());
        const bool isPassword = authType == "password"_L1;

        if (!isOAuth && !isPassword)
            continue;

        const auto email = acc.value("email"_L1).toString();
        const auto host  = acc.value("imapHost"_L1).toString();
        const int  port  = acc.value("imapPort"_L1).toInt();

        if (email.isEmpty() || host.isEmpty() || port <= 0)
            continue;

        QString credential;
        if (isOAuth) {
            credential = refreshAccessToken(acc, email);
        } else if (m_vault) {
            credential = m_vault->loadPassword(email);
        }

        if (credential.isEmpty())
            continue;

        result.push_back({email, host, credential, port, isPassword ? "password"_L1 : "oauth2"_L1});
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
                                qint32 fetchBudget) {
    auto pooled = poolForEmail(email)->acquire("fetch-folder-headers", email);
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

std::function<void()>
ImapService::makeSyncFlushLambda(QVariantList &pendingHeaders,
                                 QElapsedTimer &flushTimer) {
    return [this, &pendingHeaders, &flushTimer]() {
        if (pendingHeaders.isEmpty()) {
            return;
        }

        const QVariantList batch = pendingHeaders;
        pendingHeaders.clear();

        // Write headers directly from the worker thread (DataStore is thread-safe).
        static constexpr qsizetype kDbChunk = 5;
        for (qsizetype start = 0; start < batch.size(); start += kDbChunk) {
            const QVariantList chunk = batch.mid(start, kDbChunk);
            if (m_store) {
                m_store->upsertHeaders(chunk);
            }
        }

        // Body-fetch dispatch posted to UI thread (backgroundFetchBodies needs it).
        QMetaObject::invokeMethod(this, [this, batch]() {
            if (!m_store) {
                return;
            }
            QSet<QString> dispatched;
            for (const QVariant &hv : batch) {
                const QVariantMap h = hv.toMap();
                const QString email = h.value("accountEmail"_L1).toString().trimmed();
                const QString folder = h.value("folder"_L1).toString().trimmed();
                if (email.isEmpty() || folder.isEmpty()) {
                    continue;
                }
                const QString key = email.toLower() + "|"_L1 + folder.toLower();
                if (dispatched.contains(key)) {
                    continue;
                }
                dispatched.insert(key);
                backgroundFetchBodies({}, email, folder, {});
            }
        }, Qt::QueuedConnection);

        flushTimer.restart();
    };
}

QVariantList
ImapService::syncFolderInternal(const AccountInfo &account,
                                  const QString &target,
                                  const SyncFolderOptions &options,
                                  qint32 &seqNum,
                                  qint32 &inboxInserted,
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
    const qint32 fetchTarget = Imap::SyncUtils::recentFetchCount();
    qint32 budget;
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
            emit syncFinished(true, "Offline mode: skipped folder refresh."_L1);
        return;
    }

    const auto accounts = accountConfigList();
    if (accounts.isEmpty() || !m_store) {
        if (announce)
            emit syncFinished(false, accounts.isEmpty()
                ? "No accounts configured yet."_L1
                : "Sync dependencies not initialized."_L1);
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

                auto pooled = poolForEmail(email)->acquire("refresh-folder-list", email);
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
    if (m_destroying)
        return;

    const auto accounts = accountConfigList();
    if (accounts.isEmpty()) {
        if (!m_googleCalendarList.isEmpty()) {
            m_googleCalendarList.clear();
            emit googleCalendarListChanged();
        }
        return;
    }

    runBackgroundTask([this, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [](const AccountInfo &info) {
            return info.host.contains("gmail", Qt::CaseInsensitive);
        });
        if (it == resolved.end())
            return;
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty())
            return;

        QNetworkAccessManager nam;
        QUrl listUrl("https://www.googleapis.com/calendar/v3/users/me/calendarList"_L1);
        QUrlQuery listQuery;
        listQuery.addQueryItem("colorRgbFormat"_L1, "true"_L1);
        listUrl.setQuery(listQuery);
        QNetworkRequest req{listUrl};
        req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

        QEventLoop loop;
        QNetworkReply *reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray payload = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString err = reply->errorString();
        const qint32 httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString bodyText = QString::fromUtf8(payload).trimmed();
        reply->deleteLater();
        if (!ok) {
            qWarning().noquote() << "[calendar][google] calendarList fetch failed"
                                 << "httpStatus=" << httpStatus
                                 << "error=" << err
                                 << "body=" << bodyText;
            QMetaObject::invokeMethod(this, [this, httpStatus]() {
                Q_UNUSED(httpStatus)
                emit realtimeStatus(false, "Calendar sync error. Please reconnect your Google account and try again."_L1);
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
    if (m_destroying)
        return;

    const auto accounts = accountConfigList();
    if (accounts.isEmpty() || calendarIds.isEmpty()) {
        if (!m_googleWeekEvents.isEmpty()) {
            m_googleWeekEvents.clear();
            emit googleWeekEventsChanged();
        }
        return;
    }

    // Build calendarId → backgroundColor map from the cached calendar list.
    QHash<QString, QString> calendarColorMap;
    for (const auto &entry : m_googleCalendarList) {
        const auto m = entry.toMap();
        calendarColorMap.insert(m.value("id"_L1).toString(),
                                m.value("color"_L1).toString());
    }

    runBackgroundTask([this, accounts, calendarIds, weekStartIso, weekEndIso, calendarColorMap]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [](const AccountInfo &info) {
            return info.host.contains("gmail", Qt::CaseInsensitive);
        });
        if (it == resolved.end())
            return;
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty())
            return;

        const QDateTime weekStart = QDateTime::fromString(weekStartIso, Qt::ISODate);
        const QDateTime weekEnd = QDateTime::fromString(weekEndIso, Qt::ISODate);
        if (!weekStart.isValid() || !weekEnd.isValid())
            return;

        const qint32 totalDays = static_cast<qint32>(weekStart.date().daysTo(weekEnd.date())) - 1;

        QNetworkAccessManager nam;
        QVariantList out;

        for (const QString &calendarId : calendarIds) {
            QUrl evUrl("https://www.googleapis.com/calendar/v3/calendars/%1/events"_L1
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId))));
            QUrlQuery q;
            q.addQueryItem("singleEvents"_L1, "true"_L1);
            q.addQueryItem("orderBy"_L1, "startTime"_L1);
            q.addQueryItem("timeMin"_L1, weekStart.toUTC().toString(Qt::ISODate));
            q.addQueryItem("timeMax"_L1, weekEnd.toUTC().toString(Qt::ISODate));
            evUrl.setQuery(q);

            QNetworkRequest req{evUrl};
            req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

            QEventLoop loop;
            QNetworkReply *reply = nam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            const qint32 httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
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
                    emit realtimeStatus(false, "Calendar events couldn’t be loaded right now. Please try again."_L1);
                }, Qt::QueuedConnection);
                continue;
            }

            const auto doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject())
                continue;

            const auto items = doc.object().value("items").toArray();
            for (const auto &v : items) {
                const auto o = v.toObject();
                const QString eventId = o.value("id").toString();
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

                // Per-event color: use event's own backgroundColor if present, else the calendar's color.
                QString eventColor = o.value("backgroundColor").toString();
                if (eventColor.isEmpty())
                    eventColor = calendarColorMap.value(calendarId);

                // Recurrence description (from the expanded instance's recurringEventId).
                const QString recurrence = o.contains("recurringEventId")
                                           ? o.value("recurrence").toArray().isEmpty()
                                             ? "Recurring"_L1
                                             : o.value("recurrence").toArray().first().toString()
                                           : QString();

                // Organizer display name or email, and whether this is someone else's event.
                const auto organizerObj = o.value("organizer").toObject();
                const QString organizer = organizerObj.value("displayName").toString().isEmpty()
                    ? organizerObj.value("email").toString()
                    : organizerObj.value("displayName").toString();
                const bool organizerIsSelf = organizerObj.value("self").toBool(false);

                // Find the current user's response status from attendees.
                QString selfResponseStatus;
                const auto attendees = o.value("attendees").toArray();
                for (const auto &att : attendees) {
                    const auto a = att.toObject();
                    if (a.value("self").toBool(false)) {
                        selfResponseStatus = a.value("responseStatus").toString();
                        break;
                    }
                }

                // ISO start time for QML sorting/formatting.
                const QString startIso = startDt.toString(Qt::ISODate);

                if (isAllDay) {
                    const qint64 rawStart = weekStart.date().daysTo(startDt.date());
                    const qint64 rawEnd   = weekStart.date().daysTo(endDt.date().addDays(-1));
                    const qint32 visStart = qMax(0, static_cast<qint32>(rawStart));
                    const qint32 visEnd   = qMin(totalDays, static_cast<qint32>(rawEnd));
                    if (visStart > totalDays || visEnd < 0)
                        continue;

                    QVariantMap row;
                    row.insert("eventId"_L1, eventId);
                    row.insert("calendarId"_L1, calendarId);
                    row.insert("dayIndex"_L1, visStart);
                    row.insert("spanDays"_L1, visEnd - visStart + 1);
                    row.insert("startHour"_L1, 0.0);
                    row.insert("durationHours"_L1, 24.0);
                    row.insert("isAllDay"_L1, true);
                    row.insert("title"_L1, o.value("summary"_L1).toString());
                    row.insert("subtitle"_L1, "All day"_L1);
                    row.insert("color"_L1, eventColor);
                    row.insert("location"_L1, o.value("location"_L1).toString());
                    row.insert("visibility"_L1, o.value("visibility"_L1).toString());
                    row.insert("recurrence"_L1, recurrence);
                    row.insert("organizer"_L1, organizer);
                    row.insert("organizerIsSelf"_L1, organizerIsSelf);
                    row.insert("selfResponseStatus"_L1, selfResponseStatus);
                    row.insert("startIso"_L1, startIso);
                    out.push_back(row);
                } else {
                    const qint64 dayIndex = weekStart.date().daysTo(startDt.date());
                    if (dayIndex < 0 || dayIndex > totalDays)
                        continue;

                    const qint32 minutes = startDt.time().hour() * 60 + startDt.time().minute();
                    const qint32 durMinutes = qMax(15, static_cast<qint32>(startDt.secsTo(endDt) / 60));

                    QVariantMap row;
                    row.insert("eventId"_L1, eventId);
                    row.insert("calendarId"_L1, calendarId);
                    row.insert("dayIndex"_L1, static_cast<qint32>(dayIndex));
                    row.insert("spanDays"_L1, 1);
                    row.insert("startHour"_L1, static_cast<double>(minutes) / 60.0);
                    row.insert("durationHours"_L1, static_cast<double>(durMinutes) / 60.0);
                    row.insert("isAllDay"_L1, false);
                    row.insert("title"_L1, o.value("summary"_L1).toString());
                    row.insert("subtitle"_L1, "%1 - %2"_L1
                                     .arg(startDt.time().toString("h:mmap"_L1).toLower())
                                     .arg(endDt.time().toString("h:mmap"_L1).toLower()));
                    row.insert("color"_L1, eventColor);
                    row.insert("location"_L1, o.value("location"_L1).toString());
                    row.insert("visibility"_L1, o.value("visibility"_L1).toString());
                    row.insert("recurrence"_L1, recurrence);
                    row.insert("organizer"_L1, organizer);
                    row.insert("organizerIsSelf"_L1, organizerIsSelf);
                    row.insert("selfResponseStatus"_L1, selfResponseStatus);
                    row.insert("startIso"_L1, startIso);
                    out.push_back(row);
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, out]() {
            m_googleWeekEvents = out;
            emit googleWeekEventsChanged();
        }, Qt::QueuedConnection);
    });
}


void
ImapService::refreshGoogleContacts() {
    if (m_destroying)
        return;

    const auto accounts = accountConfigList();
    if (accounts.isEmpty())
        return;

    runBackgroundTask([this, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [](const AccountInfo &info) {
            return info.host.contains("gmail", Qt::CaseInsensitive);
        });
        if (it == resolved.end()) { return; }
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty()) { return; }

        QNetworkAccessManager nam;
        QVariantList out;
        QString nextPageToken;

        do {
            QUrl url("https://people.googleapis.com/v1/people/me/connections"_L1);
            QUrlQuery q;
            q.addQueryItem("personFields"_L1, "names,emailAddresses,phoneNumbers,photos,organizations"_L1);
            q.addQueryItem("pageSize"_L1, "1000"_L1);
            q.addQueryItem("sortOrder"_L1, "LAST_NAME_ASCENDING"_L1);
            if (!nextPageToken.isEmpty())
                q.addQueryItem("pageToken"_L1, nextPageToken);
            url.setQuery(q);

            QNetworkRequest req{url};
            req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());

            QEventLoop loop;
            QNetworkReply *reply = nam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok) {
                qWarning() << "[contacts][google] fetch failed";
                break;
            }

            const auto doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject()) { break; }

            const auto connections = doc.object().value("connections").toArray();
            for (const auto &v : connections) {
                const auto p = v.toObject();

                const auto names = p.value("names").toArray();
                const auto emails = p.value("emailAddresses").toArray();
                const auto phones = p.value("phoneNumbers").toArray();
                const auto photos = p.value("photos").toArray();
                const auto orgs = p.value("organizations").toArray();

                QString displayName;
                QString givenName;
                QString familyName;
                if (!names.isEmpty()) {
                    const auto n = names[0].toObject();
                    displayName = n.value("displayName").toString();
                    givenName = n.value("givenName").toString();
                    familyName = n.value("familyName").toString();
                }
                if (displayName.isEmpty()) { continue; }

                QVariantList emailList;
                for (const auto &e : emails) {
                    const auto eo = e.toObject();
                    emailList.push_back(QVariantMap{
                        {"value"_L1, eo.value("value").toString()},
                        {"type"_L1, eo.value("type").toString()}
                    });
                }

                QVariantList phoneList;
                for (const auto &ph : phones) {
                    const auto po = ph.toObject();
                    phoneList.push_back(QVariantMap{
                        {"value"_L1, po.value("value").toString()},
                        {"type"_L1, po.value("type").toString()}
                    });
                }

                QString photoUrl;
                if (!photos.isEmpty()) {
                    const auto photo = photos[0].toObject();
                    if (!photo.value("default").toBool(false))
                        photoUrl = photo.value("url").toString();
                }

                QString organization;
                QString title;
                if (!orgs.isEmpty()) {
                    const auto org = orgs[0].toObject();
                    organization = org.value("name").toString();
                    title = org.value("title").toString();
                }

                QVariantMap row;
                row.insert("resourceName"_L1, p.value("resourceName").toString());
                row.insert("displayName"_L1, displayName);
                row.insert("givenName"_L1, givenName);
                row.insert("familyName"_L1, familyName);
                row.insert("emails"_L1, emailList);
                row.insert("phones"_L1, phoneList);
                row.insert("photoUrl"_L1, photoUrl);
                row.insert("organization"_L1, organization);
                row.insert("title"_L1, title);
                out.push_back(row);
            }

            nextPageToken = doc.object().value("nextPageToken").toString();
        } while (!nextPageToken.isEmpty());

        std::ranges::sort(out, [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("displayName"_L1).toString()
                       .compare(b.toMap().value("displayName"_L1).toString(), Qt::CaseInsensitive) < 0;
        });

        qInfo() << "[contacts][google] fetched" << out.size() << "contacts";

        QMetaObject::invokeMethod(this, [this, out]() {
            m_googleContacts = out;
            emit googleContactsChanged();
        }, Qt::QueuedConnection);
    });
}

void
ImapService::respondToCalendarInvite(const QString &calendarId,
                                      const QString &eventId,
                                      const QString &response) {
    if (calendarId.isEmpty() || eventId.isEmpty() || response.isEmpty())
        return;

    const auto accounts = accountConfigList();
    runBackgroundTask([this, calendarId, eventId, response, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [](const AccountInfo &info) {
            return info.host.contains("gmail", Qt::CaseInsensitive);
        });
        if (it == resolved.end()) { return; }
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty()) { return; }

        const QString encodedCalId = QString::fromUtf8(QUrl::toPercentEncoding(calendarId));
        const QString encodedEvtId = QString::fromUtf8(QUrl::toPercentEncoding(eventId));

        // GET the event to retrieve current attendees.
        QUrl getUrl("https://www.googleapis.com/calendar/v3/calendars/%1/events/%2"_L1
                    .arg(encodedCalId, encodedEvtId));

        QNetworkAccessManager nam;
        QNetworkRequest getReq{getUrl};
        getReq.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());

        QEventLoop loop;
        QNetworkReply *getReply = nam.get(getReq);
        QObject::connect(getReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray getPayload = getReply->readAll();
        const bool getOk = getReply->error() == QNetworkReply::NoError;
        getReply->deleteLater();
        if (!getOk) {
            qWarning() << "[calendar] GET event failed for RSVP:" << eventId;
            return;
        }

        auto eventObj = QJsonDocument::fromJson(getPayload).object();
        auto attendees = eventObj.value("attendees").toArray();

        // Update self's responseStatus.
        bool found = false;
        for (qsizetype i = 0; i < attendees.size(); ++i) {
            auto a = attendees[i].toObject();
            if (a.value("self").toBool(false)) {
                a["responseStatus"] = response;
                attendees[i] = a;
                found = true;
                break;
            }
        }
        if (!found) {
            qWarning() << "[calendar] Self not found in attendees for" << eventId;
            return;
        }

        // PATCH the event with updated attendees.
        QJsonObject patchBody;
        patchBody["attendees"] = attendees;

        QUrl patchUrl(getUrl);
        QUrlQuery pq;
        pq.addQueryItem("sendUpdates"_L1, "all"_L1);
        patchUrl.setQuery(pq);

        QNetworkRequest patchReq{patchUrl};
        patchReq.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
        patchReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

        QNetworkReply *patchReply = nam.sendCustomRequest(
            patchReq, "PATCH", QJsonDocument(patchBody).toJson(QJsonDocument::Compact));
        QObject::connect(patchReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const bool patchOk = patchReply->error() == QNetworkReply::NoError;
        patchReply->deleteLater();

        if (patchOk) {
            qInfo() << "[calendar] RSVP sent:" << response << "for" << eventId;
            QMetaObject::invokeMethod(this, [this]() {
                emit calendarInviteResponded();
            }, Qt::QueuedConnection);
        } else {
            qWarning() << "[calendar] PATCH event failed for RSVP:" << eventId;
        }
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

    if (m_destroying || !m_store || emailNorm.isEmpty() || uidNorm.isEmpty())
        return;

    if (m_offlineMode) {
        if (userInitiated)
            emit hydrateStatus(false, "Offline mode is enabled. Body hydration is paused."_L1);
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

    const auto configs = accountConfigList();
    auto acctIt = std::ranges::find_if(configs, [&](const QVariant &a) {
        return a.toMap().value("email"_L1).toString() == emailNorm;
    });
    const QVariantMap account = (acctIt != configs.end()) ? acctIt->toMap() : QVariantMap{};

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

    // Show a toast if the hydration is still in progress after 5 seconds.
    QTimer *slowTimer = nullptr;
    if (userInitiated) {
        slowTimer = new QTimer(this);
        slowTimer->setSingleShot(true);
        slowTimer->setInterval(5000);
        connect(slowTimer, &QTimer::timeout, this, [this]() {
            emit hydrateStatus(true, "Still loading message body\u2026"_L1);
        });
        slowTimer->start();
    }

    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, slowTimer, emailNorm, folderNorm, uidNorm, inFlightKey, userInitiated]() {
        if (slowTimer) {
            slowTimer->stop();
            slowTimer->deleteLater();
        }

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
        const bool hasHtmlish = Kestrel::htmlishRe().match(htmlTrim).hasMatch();
        const bool hasMarkdownLinks = QRegularExpression("\\[[^\\]\\n]{1,240}\\]\\(https?://[^\\s)]+\\)"_L1,
                                                         QRegularExpression::CaseInsensitiveOption).match(htmlTrim).hasMatch();
        const bool hasMimeHeaders = htmlLower.contains("content-type:"_L1)
                                 || htmlLower.contains("mime-version:"_L1);
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

        const bool isOAuth = account.value("authType"_L1).toString() == "oauth2"_L1
                          || (!account.value("oauthClientId"_L1).toString().isEmpty()
                           && !account.value("oauthTokenUrl"_L1).toString().isEmpty());
        const bool isPassword = account.value("authType"_L1).toString() == "password"_L1;
        if (!isOAuth && !isPassword) {
            return {};
        }

        // Serialize all background hydrations to a single connection slot so
        // the pool stays available for user actions, syncs, and IDLE.
        // User-initiated hydrations skip this entirely.
        auto *hydratePool = poolForEmail(emailCopy);
        const bool isBg = !userInitiated;
        if (isBg && hydratePool && !hydratePool->tryAcquireBgHydrate(60'000))
            return {};
        struct SemGuard {
            Imap::ConnectionPool *pool; bool active;
            ~SemGuard() { if (active && pool) pool->releaseBgHydrate(); }
        } semGuard{hydratePool, isBg};

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

        // Retry loop: on failure for user-initiated hydrations, wait for
        // a fresh OAuth token and try once more (up to 60 s total).
        constexpr int kHydrateRetryDelayMs = 3000;
        constexpr int kHydrateMaxTotalMs   = 60000;
        const qint32 maxAttempts = userInitiated ? 3 : 1;

        for (qint32 attempt = 0; attempt < maxAttempts; ++attempt) {
            if (m_destroying)
                return QString{};
            if (attempt > 0 && hydrateTimer.elapsed() >= kHydrateMaxTotalMs)
                break;

            // Back off before retries so the token refresh has time to land.
            if (attempt > 0)
                QThread::msleep(kHydrateRetryDelayMs);

            auto *pool = poolForEmail(emailCopy);
            std::shared_ptr<Imap::Connection> pooled;
            bool usedDedicated = false;
            if (userInitiated) {
                pooled = pool->acquireHydrate(emailCopy);
                if (pooled) {
                    usedDedicated = true;
                } else {
                    qint8 poolAttempts = 0;
                    pooled = pool->acquire("hydrate-user"_L1, emailCopy);
                    while (!pooled && poolAttempts++ < 10)
                        pooled = pool->acquire("hydrate-user-fallback"_L1, emailCopy);
                }
            } else {
                pooled = pool->acquire("bg-hydrate"_L1, emailCopy);
            }

            const qint64 acquireMs = hydrateTimer.elapsed() - mapMs;

            if (!pooled) {
                if (attempt == maxAttempts - 1) {
                    if (userInitiated)
                        emit hydrateStatus(false, "Message body fetch failed: IMAP connection pool timeout.");
                    qWarning().noquote() << "[perf-hydrate]"
                                         << "uid=" << uidNorm
                                         << "mapMs=" << mapMs
                                         << "acquireMs=" << acquireMs
                                         << "result=pool-timeout";
                }
                continue;
            }

            r.cxn = std::move(pooled);
            const QString html = Imap::MessageHydrator::execute(r);
            const qint64 totalMs = hydrateTimer.elapsed();
            const qint64 executeMs = totalMs - mapMs - acquireMs;
            qWarning().noquote() << "[perf-hydrate]"
                                 << "uid=" << uidNorm
                                 << "source=" << (userInitiated ? (usedDedicated ? "user-dedicated" : "user-pool") : "bg")
                                 << "attempt=" << (attempt + 1)
                                 << "mapMs=" << mapMs
                                 << "acquireMs=" << acquireMs
                                 << "executeMs=" << executeMs
                                 << "totalMs=" << totalMs
                                 << "htmlLen=" << html.size();

            if (!html.isEmpty())
                return html;

            // Release the (possibly dead) connection before retrying.
            r.cxn.reset();
        }
        return QString{};
    }));
}

void
ImapService::moveMessage(const QString &accountEmail, const QString &folder,
                          const QString &uid, const QString &targetFolder) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty() || targetFolder.isEmpty())
        return;

    qint64 messageId = -1;
    qint32 unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

        const QString tgt = targetFolder.trimmed().toLower();
        const bool movingToTrash = tgt == "trash"_L1
                                || tgt == "[gmail]/trash"_L1
                                || tgt == "[google mail]/trash"_L1
                                || tgt.endsWith("/trash"_L1);
        if (movingToTrash)
            m_store->removeAccountUidsEverywhere(accountEmail, {uid}, /*skipOrphanCleanup=*/true);
        else
            m_store->deleteSingleFolderEdge(accountEmail, folder, uid);
    }

    if (m_offlineMode) {
        emit realtimeStatus(true, "Offline mode: skipped IMAP move for %1."_L1.arg(uid));
        return;
    }

    (void)QtConcurrent::run([this, accountEmail, folder, uid, targetFolder, messageId, unreadVal]() {
        if (m_destroying.load()) return;

        auto cxn = poolForEmail(accountEmail)->acquire("move-message", accountEmail);
        if (!cxn) return;

        const auto sourceFolder = resolveSourceFolder(folder);

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(sourceFolder, Qt::CaseInsensitive) != 0) {
            if (!cxn->select(sourceFolder)) return;
        }

        const QString resp = cxn->execute("UID MOVE %1 \"%2\""_L1.arg(uid, targetFolder));

        qInfo().noquote() << "[move-message]"
                          << "uid=" << uid << "from=" << folder
                          << "to=" << targetFolder << "resp=" << resp.simplified().left(120);

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

    (void)QtConcurrent::run([this, accountEmail, folder, uid]() {
        if (m_destroying.load()) { return; }

        auto cxn = poolForEmail(accountEmail)->acquire("mark-read", accountEmail);
        if (!cxn) { return; }

        const auto sourceFolder = resolveSourceFolder(folder);

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(sourceFolder, Qt::CaseInsensitive) != 0) {
            if (!cxn->select(sourceFolder)) { return; }
        }

        (void)cxn->execute("UID STORE %1 +FLAGS (\\Seen)"_L1.arg(uid));
    });
}

void
ImapService::markMessageFlagged(const QString &accountEmail, const QString &folder, const QString &uid, const bool flagged) {
    if (m_destroying || accountEmail.isEmpty() || uid.isEmpty()) { return; }

    if (m_store) {
        m_store->markMessageFlagged(accountEmail, uid, flagged);
    }

    if (m_offlineMode) { return; }

    const auto flags = flagged ? "+FLAGS (\\Flagged)"_L1 : "-FLAGS (\\Flagged)"_L1;
    (void)QtConcurrent::run([this, accountEmail, folder, uid, flags]() {
        if (m_destroying.load()) { return; }

        auto cxn = poolForEmail(accountEmail)->acquire("flag-message", accountEmail);
        if (!cxn) { return; }

        const auto sourceFolder = resolveSourceFolder(folder);

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(sourceFolder, Qt::CaseInsensitive) != 0) {
            if (!cxn->select(sourceFolder)) { return; }
        }

        (void)cxn->execute("UID STORE %1 %2"_L1.arg(uid, flags));
    });
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

            if (desired == "important"_L1) {
                if (special == "important"_L1 || lname.endsWith("/important"_L1)) {
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
    qint32 unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

        // Optimistic local membership so UI reflects the tag immediately.
        if (messageId > 0)
            m_store->insertFolderEdge(accountEmail, messageId, resolvedTarget, uid, unreadVal);
    }

    if (m_offlineMode) {
        emit realtimeStatus(true, "Offline mode: skipped IMAP add-to-folder for %1."_L1.arg(uid));
        return;
    }

    (void)QtConcurrent::run([this, accountEmail, folder, uid, resolvedTarget, messageId, unreadVal]() {
        if (m_destroying.load()) return;

        auto cxn = poolForEmail(accountEmail)->acquire("add-folder-membership", accountEmail);
        if (!cxn) return;

        const auto sourceFolder = resolveSourceFolder(folder);

        if (cxn->isSelectedReadOnly() || cxn->selectedFolder().compare(sourceFolder, Qt::CaseInsensitive) != 0) {
            if (!cxn->select(sourceFolder)) return;
        }

        const QString resp = cxn->execute("UID COPY %1 \"%2\""_L1.arg(uid, resolvedTarget));

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
ImapService::copyToLocalFolder(const QString &accountEmail, const QString &folder,
                                const QString &uid, const QString &localFolderKey) {
    if (m_destroying || accountEmail.isEmpty() || folder.isEmpty() || uid.isEmpty() || localFolderKey.isEmpty())
        return;

    qint64 messageId = -1;
    qint32 unreadVal = 0;
    if (m_store) {
        const QVariantMap edge = m_store->folderMapRowForEdge(accountEmail, folder, uid);
        messageId = edge.value("messageId"_L1, -1LL).toLongLong();
        unreadVal = edge.value("unread"_L1, 0).toInt();

        if (messageId <= 0) {
            qWarning() << "[copy-to-local] No message_id for" << accountEmail << folder << uid;
            return;
        }

        // Optimistic: insert local folder edge so the message appears immediately.
        m_store->insertFolderEdge(accountEmail, messageId, localFolderKey, uid, unreadVal, "local-copy"_L1);
        m_store->notifyDataChanged();
    }

    // Background: ensure body is hydrated and download attachments to persistent storage.
    const auto msgId = messageId;
    runBackgroundTask([this, accountEmail, folder, uid, localFolderKey, msgId]() {
        if (m_destroying.load()) { return; }

        // 1. Hydrate body if missing.
        if (m_store && !m_store->hasUsableBodyForEdge(accountEmail, folder, uid)) {
            auto cxn = poolForEmail(accountEmail)->acquire("local-copy-hydrate", accountEmail);
            if (cxn) {
                Imap::MessageHydrator::Request req;
                req.cxn        = cxn;
                req.folderName = folder;
                req.uid        = uid;
                const QString html = Imap::MessageHydrator::execute(req);
                if (!html.isEmpty()) {
                    QMetaObject::invokeMethod(this, [this, accountEmail, folder, uid, html]() {
                        if (!m_destroying.load() && m_store)
                            m_store->updateBodyForKey(accountEmail, folder, uid, html);
                    }, Qt::QueuedConnection);
                }
            }
        }

        // 2. Download attachments to persistent local storage.
        if (!m_store) { return; }
        const QVariantList attachments = m_store->attachmentsForMessage(accountEmail, folder, uid);
        if (attachments.isEmpty()) { return; }

        auto cxn = poolForEmail(accountEmail)->acquire("local-copy-attachments", accountEmail);
        if (!cxn) { return; }

        const auto sourceFolder = resolveSourceFolder(folder);
        if (cxn->selectedFolder().compare(sourceFolder, Qt::CaseInsensitive) != 0) {
            if (!cxn->examine(sourceFolder)) { return; }
        }

        const auto storageBase = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                               + "/local-attachments/"_L1 + accountEmail
                               + "/"_L1 + QString::number(msgId);

        for (const auto &a : attachments) {
            if (m_destroying.load()) { break; }

            const auto am       = a.toMap();
            const QString partId   = am.value("partId"_L1).toString();
            const QString encoding = am.value("encoding"_L1).toString();
            const QString name     = am.value("name"_L1).toString();

            const auto data = fetchAttachmentPartById(*cxn, uid, partId, encoding, [](int, qint64) {});
            if (data.isEmpty()) { continue; }

            const auto safePartId = QString(partId).replace('/', '_').replace('.', '_');
            const auto partDir    = storageBase + "/"_L1 + safePartId;
            QDir().mkpath(partDir);

            const auto safeName  = QString(name).replace(QRegularExpression("[^A-Za-z0-9._-]"_L1), "_"_L1);
            const auto localPath = partDir + "/"_L1 + (safeName.isEmpty() ? "attachment"_L1 : safeName);

            QSaveFile f(localPath);
            if (!f.open(QIODevice::WriteOnly)) { continue; }
            f.write(data);
            if (!f.commit()) { continue; }

            // Persist the local path in the DB so it survives cache expiry.
            QMetaObject::invokeMethod(this, [this, accountEmail, msgId, partId, localPath]() {
                if (!m_destroying.load() && m_store)
                    m_store->setAttachmentLocalPath(accountEmail, msgId, partId, localPath);
            }, Qt::QueuedConnection);

            // Also add to the in-memory cache for immediate access.
            const auto cacheKey = accountEmail + "|"_L1 + uid + "|"_L1 + partId;
            attachmentCacheInsert(cacheKey, localPath);
        }

        qInfo().noquote() << "[copy-to-local] Done:"
                          << "account=" << accountEmail << "uid=" << uid
                          << "localFolder=" << localFolderKey
                          << "attachments=" << attachments.size();
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

            if (desired == "important"_L1) {
                if (special == "important"_L1 || lname.endsWith("/important"_L1)) {
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
        emit realtimeStatus(true, "Offline mode: skipped IMAP remove-from-folder for %1."_L1.arg(uid));
        return;
    }

    (void)QtConcurrent::run([this, accountEmail, folder, uid, resolvedTarget, targetUid]() {
        if (m_destroying.load())
            return;

        auto cxn = poolForEmail(accountEmail)->acquire("remove-folder-membership", accountEmail);
        if (!cxn)
            return;

        if (!targetUid.isEmpty()) {
            // Standard IMAP folder removal: select the label folder, mark deleted, expunge.
            // On Gmail this removes the label without permanently deleting the message
            // (it remains in [Gmail]/All Mail and any other labeled folders).
            if (cxn->select(resolvedTarget)) {
                (void)cxn->execute("UID STORE %1 +FLAGS (\\Deleted)"_L1.arg(targetUid));
                (void)cxn->execute("UID EXPUNGE %1"_L1.arg(targetUid));
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
ImapService::syncFolder(const QString &folderName, bool announce, const QString &accountEmail) {
    if (m_destroying)
        return;

    const auto target = folderName.trimmed().isEmpty() ? "INBOX"_L1 : folderName.trimmed();

    if (m_offlineMode) {
        if (announce)
            emit syncFinished(true, "Offline mode: skipped sync for %1."_L1.arg(target));
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

    if (resolveSourceFolder(target) != target) {
        if (announce) {
            emit syncFinished(true, "Category folders are managed via labels.");
        }
        releaseSyncTarget();
        return;
    }


    const auto accounts = accountConfigList();
    if (accounts.isEmpty() || !m_store) {
        if (announce)
            emit syncFinished(false, accounts.isEmpty()
                ? "No accounts configured yet."_L1
                : "Sync dependencies not initialized."_L1);
        releaseSyncTarget();
        return;
    }

    runAsync(
        [this, accounts, target, announce, accountEmail]() -> SyncResult {
            // Cap concurrent background syncs to keep pool connections available.
            auto *syncPool = poolForEmail(accountEmail);
            const bool isBgSync = !announce;
            if (isBgSync && syncPool && !syncPool->tryAcquireBgFolderSync(30'000))
                return SyncResult{};
            struct FolderSyncGuard {
                Imap::ConnectionPool *pool; bool active;
                ~FolderSyncGuard() { if (active && pool) pool->releaseBgFolderSync(); }
            } fsgGuard{syncPool, isBgSync};

            SyncResult result;
            qint32 inboxInserted = 0;
            qint32 seqNum = 0;
            QVariantList pendingHeaders;
            QElapsedTimer flushTimer;
            flushTimer.start();
            auto flush = makeSyncFlushLambda(pendingHeaders, flushTimer);

            for (const auto &[email, host, accessToken, port, acctAuthType] : resolveAccounts(accounts)) {
                if (!accountEmail.isEmpty() && email.compare(accountEmail, Qt::CaseInsensitive) != 0)
                    continue;
                if (m_cancelRequested.load()) {
                    SyncResult r;
                    r.message = "Aborted fetch for %1."_L1.arg(target);
                    return r;
                }

                (void)syncFolderInternal(AccountInfo{email, host, accessToken, port, acctAuthType}, target, SyncFolderOptions{announce},
                                           seqNum, inboxInserted, pendingHeaders, result.headers,
                                           flushTimer, flush);
            }

            // Final flush handles any remaining headers and dispatches backgroundFetchBodies.
            flush();
            result.ok      = true;
            result.inserted = inboxInserted;
            result.message  = "%1 sync complete."_L1.arg(target);

            return result;
        },
        [this, announce, target, releaseSyncTarget](const SyncResult &r) {
            const QString folderLabel = stripGmailPrefix(target);

            QSet<QString> uniqueUids;
            uniqueUids.reserve(r.headers.size());
            for (const QVariant &hv : r.headers) {
                const QString uid = hv.toMap().value("uid"_L1).toString();
                if (!uid.isEmpty())
                    uniqueUids.insert(uid);
            }
            const qint32 syncedCount = uniqueUids.isEmpty() ? static_cast<qint32>(r.headers.size()) : static_cast<qint32>(uniqueUids.size());

            if (announce) {
                if (!r.ok)
                    emit syncFinished(false, r.message);
                else
                    emit syncFinished(true, syncedCount > 0
                        ? QStringLiteral("%1 synced %2 messages.").arg(folderLabel).arg(syncedCount)
                        : QString());
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

QStringList
ImapService::syncTargetsForAccount(const QString &email, const QString &host) const {
    QStringList targets = { "INBOX"_L1 };
    QSet<QString> seen  = { "inbox"_L1 };

    if (!m_store) return targets;

    // Read the account's folders from the DB.
    const auto allFolders = m_store->folders();

    QSet<QString> parentContainers;
    for (const auto &fv : allFolders) {
        const auto f = fv.toMap();
        if (f.value("accountEmail"_L1).toString().compare(email, Qt::CaseInsensitive) != 0)
            continue;
        const auto name = f.value("name"_L1).toString().trimmed();
        if (const auto slash = name.indexOf('/'); slash > 0)
            parentContainers.insert(name.left(slash).toLower());
    }

    for (const auto &fv : allFolders) {
        const auto f = fv.toMap();
        if (f.value("accountEmail"_L1).toString().compare(email, Qt::CaseInsensitive) != 0)
            continue;
        const auto name = f.value("name"_L1).toString().trimmed();
        const auto flags = f.value("flags"_L1).toString().toLower();
        const auto canonName = stripGmailPrefix(name).toLower();

        if (name.isEmpty()
            || name.contains("/Categories/"_L1, Qt::CaseInsensitive)
            || name.compare("[Gmail]"_L1, Qt::CaseInsensitive) == 0
            || name.compare("[Google Mail]"_L1, Qt::CaseInsensitive) == 0
            || flags.contains("\\noselect"_L1)
            || parentContainers.contains(name.toLower()))
            continue;

        if (!seen.contains(canonName)) {
            seen.insert(canonName);
            targets << name;
        }
    }

    // Gmail fallback when folder list is empty.
    const bool isGmailHost = host.contains("gmail.com"_L1, Qt::CaseInsensitive)
                          || host.contains("google"_L1, Qt::CaseInsensitive);
    if (targets.size() == 1 && isGmailHost) {
        targets << "[Gmail]/All Mail"_L1 << "[Gmail]/Sent Mail"_L1
                << "[Gmail]/Drafts"_L1 << "[Gmail]/Spam"_L1 << "[Gmail]/Trash"_L1;
    }

    return targets;
}

void
ImapService::refreshGooglePeopleAvatars(const QString &accountEmail) {
    if (m_destroying || !m_store) return;

    const auto accounts = accountConfigList();
    runBackgroundTask([this, accounts, accountEmail]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [&](const AccountInfo &info) {
            return info.email.compare(accountEmail, Qt::CaseInsensitive) == 0;
        });
        if (it == resolved.end()) return;
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty()) return;

        QStringList staleEmails;
        if (m_store)
            staleEmails = m_store->staleGooglePeopleEmails();

        for (const QString &sEmail : staleEmails) {
            if (m_destroying.load()) break;
            const QString url = resolveGooglePeopleAvatarUrl(sEmail, accessToken);
            if (url.isEmpty()) continue;
            const QString blob = fetchAvatarBlob(url, accessToken);
            if (!blob.startsWith("data:"_L1)) continue;
            const QString fileUrl = writeAvatarFile(sEmail, blob);
            if (fileUrl.isEmpty()) continue;
            if (m_store) m_store->updateContactAvatar(sEmail, fileUrl, "google-people"_L1);
        }
    });
}

