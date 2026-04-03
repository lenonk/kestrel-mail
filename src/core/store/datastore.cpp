#include "datastore.h"
#include "contactstore.h"
#include "folderstatsstore.h"
#include "messagestore.h"
#include "userprefsstore.h"

#include "../utils.h"

#include <QDateTime>
#include <QLocale>
#include <QSqlDatabase>
#include <algorithm>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QColor>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

using namespace Qt::Literals::StringLiterals;

namespace {

qint32
purgeCategoryFolderEdges(QSqlDatabase &database) {
    QSqlQuery q(database);
    q.prepare(R"(
        DELETE FROM message_folder_map
        WHERE lower(folder) LIKE '%/categories/%'
    )"_L1);

    if (!q.exec()) { return -1; }

    return q.numRowsAffected();
}

} // namespace

DataStore::DataStore(QObject *parent)
    : QObject(parent)
    , m_connectionName("kestrel_%1"_L1.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    , m_contacts(std::make_unique<ContactStore>([this]() { return db(); }, QThread::currentThread()))
    , m_prefs(std::make_unique<UserPrefsStore>([this]() { return db(); }))
    , m_folderStats(std::make_unique<FolderStatsStore>([this]() { return db(); }))
    , m_messages(std::make_unique<MessageStore>(
          [this]() { return db(); },
          *m_contacts,
          *m_folderStats,
          MessageStoreCallbacks{
              [this]() { scheduleDataChangedSignal(); },
              [this]() { return m_desktopNotifyEnabled.load(); },
              [this](const QVariantMap &info) {
                  QMetaObject::invokeMethod(this, [this, info]() {
                      emit newMailReceived(info);
                  }, Qt::QueuedConnection);
              }
          }))
{
}

DataStore::~DataStore()
{
    QMutexLocker lock(&m_connMutex);
    for (const QString &connName : std::as_const(m_threadConnections)) {
        auto d = QSqlDatabase::database(connName, false);
        if (d.isValid()) d.close();
        QSqlDatabase::removeDatabase(connName);
    }
    m_threadConnections.clear();
}

QSqlDatabase DataStore::db() const
{
    const auto tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    {
        QMutexLocker lock(&m_connMutex);
        auto it = m_threadConnections.constFind(tid);
        if (it != m_threadConnections.constEnd())
            return QSqlDatabase::database(*it);
    }

    if (m_dbPath.isEmpty())
        return QSqlDatabase::database(m_connectionName);

    // Create a new per-thread connection for this worker thread.
    const QString connName = m_connectionName + "-t"_L1 + QString::number(tid, 16);
    {
        QSqlDatabase newDb = QSqlDatabase::addDatabase("QSQLITE"_L1, connName);
        newDb.setDatabaseName(m_dbPath);
        if (!newDb.open()) {
            qWarning() << "[DataStore] failed to open per-thread DB connection for thread" << tid;
            return newDb;
        }
        QSqlQuery q(newDb);
        q.exec("PRAGMA journal_mode=WAL"_L1);
        q.exec("PRAGMA busy_timeout=5000"_L1);
        q.exec("PRAGMA synchronous=NORMAL"_L1);
    }
    {
        QMutexLocker lock(&m_connMutex);
        m_threadConnections[tid] = connName;
    }
    return QSqlDatabase::database(connName);
}

bool DataStore::init()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + "/kestrel-mail"_L1;
    QDir().mkpath(base);
    const QString path = base + "/mail.db"_L1;

    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE"_L1, m_connectionName);
    database.setDatabaseName(path);
    if (!database.open()) {
        return false;
    }

    // Store path and register this thread's connection before any queries.
    m_dbPath = path;
    {
        QMutexLocker lock(&m_connMutex);
        m_threadConnections[reinterpret_cast<quintptr>(QThread::currentThreadId())] = m_connectionName;
    }

    QSqlQuery q(database);

    // Enable WAL mode so worker-thread per-connection reads don't block writes.
    q.exec("PRAGMA journal_mode=WAL"_L1);
    q.exec("PRAGMA busy_timeout=5000"_L1);
    q.exec("PRAGMA synchronous=NORMAL"_L1);

    // Finalized schema cleanup.
    if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='canonical_messages'"_L1)
            && q.next()) {
        q.exec("DROP TABLE IF EXISTS messages"_L1);
        q.exec("ALTER TABLE canonical_messages RENAME TO messages"_L1);
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          logical_key TEXT NOT NULL,
          sender TEXT,
          recipient TEXT,
          subject TEXT,
          received_at TEXT,
          snippet TEXT,
          body_html TEXT,
          avatar_domain TEXT,
          avatar_url TEXT,
          avatar_source TEXT,
          unread INTEGER DEFAULT 1,
          has_tracking_pixel INTEGER DEFAULT 0,
          UNIQUE(account_email, logical_key)
        )
    )"_L1)) {
        return false;
    }

    // Forward-compatible migration for existing DBs.
    q.exec("ALTER TABLE messages ADD COLUMN avatar_domain TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN avatar_url TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN avatar_source TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN message_id_header TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN gm_msg_id TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN recipient TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN recipient_avatar_url TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN list_unsubscribe TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN reply_to TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN return_path TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN auth_results TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN x_mailer TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN in_reply_to TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN references_header TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN esp_vendor TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN thread_id TEXT"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_thread ON messages(account_email, thread_id)"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN gm_thr_id TEXT"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_gm_thr ON messages(account_email, gm_thr_id)"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN has_tracking_pixel INTEGER DEFAULT 0"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN cc TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN flagged INTEGER DEFAULT 0"_L1);
    q.exec("ALTER TABLE folder_sync_status ADD COLUMN last_sync_modseq INTEGER NOT NULL DEFAULT 0"_L1);

    // Backfill thread_id for existing messages.
    {
        QSqlQuery bfQ(database);
        bfQ.exec(R"(
            SELECT id, message_id_header, in_reply_to, references_header
            FROM messages WHERE thread_id IS NULL OR length(trim(thread_id)) = 0 LIMIT 10000
        )"_L1);
        QSqlQuery upQ(database);
        upQ.prepare("UPDATE messages SET thread_id=:tid WHERE id=:id"_L1);
        while (bfQ.next()) {
            const QString tid = MessageStore::computeThreadId(
                bfQ.value(3).toString(), bfQ.value(2).toString(), bfQ.value(1).toString());
            if (!tid.isEmpty()) {
                upQ.bindValue(":tid"_L1, tid);
                upQ.bindValue(":id"_L1, bfQ.value(0).toInt());
                upQ.exec();
            }
        }
    }

    // Clear raw HTTPS Google profile photo URLs from contact_avatars.
    q.exec(R"(
        UPDATE contact_avatars SET avatar_url='', failure_count=0, last_checked_at='2000-01-01T00:00:00'
        WHERE source='google-people'
        AND (avatar_url LIKE 'https://%' OR avatar_url LIKE 'http://%')
    )"_L1);

    q.exec(R"(
        UPDATE contact_avatars SET avatar_url='', failure_count=0, last_checked_at='2000-01-01T00:00:00'
        WHERE avatar_url LIKE 'https://www.google.com/s2/favicons%'
    )"_L1);

    // Migrate existing data URI avatar entries to files on disk.
    {
        QSqlQuery qSel(database);
        if (qSel.exec("SELECT email, avatar_url FROM contact_avatars WHERE avatar_url LIKE 'data:%'"_L1)) {
            struct MigrateRow { QString email; QString url; };
            QVector<MigrateRow> rows;
            while (qSel.next())
                rows.push_back({qSel.value(0).toString(), qSel.value(1).toString()});
            for (const auto &row : rows) {
                const QString fileUrl = m_contacts->writeAvatarDataUri(row.email, row.url);
                QSqlQuery qUp(database);
                qUp.prepare(
                    "UPDATE contact_avatars SET avatar_url=:url WHERE email=:email"_L1);
                qUp.bindValue(":url"_L1, fileUrl);
                qUp.bindValue(":email"_L1, row.email);
                qUp.exec();
            }
        }
    }

    // Clear stale esp_vendor values.
    q.exec(R"(
        UPDATE messages SET esp_vendor = NULL
        WHERE esp_vendor IS NOT NULL
        AND esp_vendor NOT IN ('Mailgun','Sendgrid','Mailchimp','Klaviyo','Postmark','Amazon SES')
    )"_L1);

    // Clear snippets containing raw HTML/DOCTYPE content.
    q.exec(R"(
        UPDATE messages SET snippet = NULL
        WHERE snippet IS NOT NULL AND (
          snippet LIKE '<!DOCTYPE%' OR snippet LIKE '<%html%' OR
          snippet LIKE '<img%' OR snippet LIKE '<[%' OR
          snippet LIKE '<table%' OR snippet LIKE '<div%' OR
          snippet LIKE '<style%' OR snippet LIKE '<script%'
        )
    )"_L1);

    q.exec(R"(
        UPDATE messages SET snippet = NULL
        WHERE snippet IS NOT NULL AND (
          snippet LIKE '%[%](%' OR
          snippet LIKE '%&ndash;%' OR snippet LIKE '%&mdash;%' OR
          snippet LIKE '%&hellip;%' OR snippet LIKE '%&rsquo;%' OR
          snippet GLOB '*=====*' OR snippet GLOB '*_____*'
        )
    )"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS message_folder_map (
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          folder TEXT NOT NULL,
          uid TEXT NOT NULL,
          unread INTEGER NOT NULL DEFAULT 1,
          source TEXT NOT NULL DEFAULT 'imap-label',
          confidence INTEGER NOT NULL DEFAULT 100,
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          PRIMARY KEY(account_email, folder, uid),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"_L1)) {
        return false;
    }
    q.exec("ALTER TABLE message_folder_map ADD COLUMN unread INTEGER NOT NULL DEFAULT 1"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS contact_avatars (
          email TEXT PRIMARY KEY,
          avatar_url TEXT,
          source TEXT,
          last_checked_at TEXT NOT NULL DEFAULT (datetime('now')),
          etag TEXT,
          failure_count INTEGER NOT NULL DEFAULT 0
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS contact_display_names (
          email TEXT PRIMARY KEY,
          display_name TEXT NOT NULL,
          source TEXT,
          display_score INTEGER NOT NULL DEFAULT 0,
          last_seen_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }
    q.exec("ALTER TABLE contact_display_names ADD COLUMN display_score INTEGER NOT NULL DEFAULT 0"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS message_labels (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          label TEXT NOT NULL,
          source TEXT NOT NULL DEFAULT 'imap-label',
          confidence INTEGER NOT NULL DEFAULT 100,
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          UNIQUE(account_email, message_id, label),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS message_participants (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          role TEXT NOT NULL,
          position INTEGER NOT NULL DEFAULT 0,
          display_name TEXT,
          address TEXT,
          source TEXT NOT NULL DEFAULT 'header',
          UNIQUE(account_email, message_id, role, position),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          name TEXT NOT NULL,
          normalized_name TEXT NOT NULL UNIQUE,
          color TEXT,
          origin TEXT NOT NULL DEFAULT 'server',
          created_at TEXT NOT NULL DEFAULT (datetime('now')),
          updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS message_tag_map (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          tag_id INTEGER NOT NULL,
          source TEXT NOT NULL DEFAULT 'server',
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          UNIQUE(account_email, message_id, tag_id),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE,
          FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE
        )
    )"_L1)) {
        return false;
    }

    // Backfill unified tags from observed labels.
    q.exec(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        SELECT ml.label, lower(ml.label), 'server', datetime('now')
        FROM message_labels ml
        GROUP BY lower(ml.label)
        ON CONFLICT(normalized_name) DO UPDATE SET
          updated_at=datetime('now')
    )"_L1);

    q.exec(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT ml.account_email, ml.message_id, t.id, 'server', datetime('now')
        FROM message_labels ml
        JOIN tags t ON t.normalized_name = lower(ml.label)
        ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
          observed_at=datetime('now')
    )"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS folders (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          name TEXT NOT NULL,
          flags TEXT,
          special_use TEXT,
          UNIQUE(account_email, name)
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS folder_sync_status (
          account_email TEXT NOT NULL,
          folder TEXT NOT NULL,
          uid_next INTEGER,
          highest_modseq INTEGER,
          messages INTEGER,
          updated_at TEXT NOT NULL DEFAULT (datetime('now')),
          PRIMARY KEY(account_email, folder)
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS sender_image_permissions (
          domain TEXT PRIMARY KEY,
          granted_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS message_attachments (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          message_id INTEGER NOT NULL,
          account_email TEXT NOT NULL,
          part_id TEXT NOT NULL,
          name TEXT NOT NULL DEFAULT '',
          mime_type TEXT NOT NULL DEFAULT '',
          encoded_bytes INTEGER NOT NULL DEFAULT 0,
          encoding TEXT NOT NULL DEFAULT '',
          UNIQUE(account_email, message_id, part_id),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS favorites_config (
          key     TEXT PRIMARY KEY,
          enabled INTEGER NOT NULL DEFAULT 1
        )
    )"_L1)) {
        return false;
    }

    q.exec(R"(
        INSERT OR IGNORE INTO favorites_config (key, enabled) VALUES
          ('all-inboxes', 1),
          ('unread',      1),
          ('flagged',     1),
          ('outbox',      0),
          ('sent',        0),
          ('trash',       0),
          ('drafts',      0),
          ('junk',        0),
          ('archive',     0),
          ('unreplied',   0),
          ('snoozed',     0)
    )"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS user_folders (
          id         INTEGER PRIMARY KEY AUTOINCREMENT,
          name       TEXT    NOT NULL UNIQUE,
          sort_order INTEGER NOT NULL DEFAULT 0,
          created_at TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS search_history (
          id          INTEGER PRIMARY KEY AUTOINCREMENT,
          query       TEXT    NOT NULL UNIQUE,
          searched_at TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    // Paging/list performance indexes.
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_received_at_id ON messages(received_at DESC, id DESC)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_folder_message ON message_folder_map(folder, message_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_message ON message_folder_map(account_email, message_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_folder_uid ON message_folder_map(account_email, folder, uid)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_lf_uid ON message_folder_map(account_email, lower(folder), uid)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_lower_folder ON message_folder_map(lower(folder))"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_message_unread ON message_folder_map(account_email, message_id, unread)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_ml_account_message_label ON message_labels(account_email, message_id, label)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_ml_label_lower ON message_labels(lower(label))"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mtm_account_message_tag ON message_tag_map(account_email, message_id, tag_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mtm_tag_id ON message_tag_map(tag_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mp_address_lower ON message_participants(lower(address))"_L1);

    // Cleanup: remove poisoned display names.
    q.exec(R"(
        DELETE FROM contact_display_names
        WHERE instr(display_name, '@') > 0
           OR instr(display_name, ',') > 0
           OR instr(display_name, ';') > 0
    )"_L1);

    const int purgedCategoryEdges = purgeCategoryFolderEdges(database);
    if (purgedCategoryEdges > 0) {
        qInfo().noquote() << "[migration-cleanup]" << "purgedCategoryEdges=" << purgedCategoryEdges;
    }

    // Backfill missing contact display names from participant evidence.
    q.exec(R"(
        WITH ranked AS (
            SELECT lower(address) AS email,
                   trim(display_name) AS display_name,
                   count(*) AS n,
                   row_number() OVER (
                     PARTITION BY lower(address)
                     ORDER BY count(*) DESC, length(trim(display_name)) DESC, trim(display_name) ASC
                   ) AS rn
            FROM message_participants
            WHERE address IS NOT NULL
              AND trim(address) <> ''
              AND display_name IS NOT NULL
              AND trim(display_name) <> ''
              AND instr(display_name, '@') = 0
              AND instr(display_name, ',') = 0
              AND instr(display_name, ';') = 0
            GROUP BY lower(address), trim(display_name)
        )
        INSERT INTO contact_display_names (email, display_name, source, display_score, last_seen_at)
        SELECT r.email, r.display_name, 'participant-backfill', 500, datetime('now')
        FROM ranked r
        LEFT JOIN contact_display_names c ON c.email = r.email
        WHERE r.rn = 1
          AND (c.email IS NULL OR length(trim(coalesce(c.display_name,''))) = 0)
        ON CONFLICT(email) DO UPDATE SET
          display_name = excluded.display_name,
          display_score = excluded.display_score,
          source = excluded.source,
          last_seen_at = datetime('now')
    )"_L1);

    // Repair pass for weak/empty canonical message rows.
    q.exec(R"(
        UPDATE message_folder_map AS m
        SET message_id = (
            SELECT m2.message_id
            FROM message_folder_map m2
            JOIN messages s ON s.id = m2.message_id
            WHERE m2.account_email = m.account_email
              AND m2.uid = m.uid
              AND m2.message_id <> m.message_id
              AND (
                length(trim(COALESCE(s.sender, ''))) > 0
                OR length(trim(COALESCE(s.subject, ''))) > 0
                OR length(trim(COALESCE(s.message_id_header, ''))) > 0
                OR length(trim(COALESCE(s.gm_msg_id, ''))) > 0
              )
            ORDER BY m2.rowid DESC
            LIMIT 1
        )
        WHERE EXISTS (
            SELECT 1 FROM messages w
            WHERE w.id = m.message_id
              AND length(trim(COALESCE(w.sender, ''))) = 0
              AND length(trim(COALESCE(w.subject, ''))) = 0
              AND length(trim(COALESCE(w.message_id_header, ''))) = 0
              AND length(trim(COALESCE(w.gm_msg_id, ''))) = 0
        )
    )"_L1);

    notifyDataChanged();
    reloadFolders();
    return true;
}

bool DataStore::quickCheck() const
{
    QSqlDatabase database = db();
    if (!database.isOpen())
        return false;

    QSqlQuery q(database);
    if (!q.exec("PRAGMA quick_check"_L1)) {
        qWarning() << "[DataStore] quick_check failed to execute:" << q.lastError().text();
        return false;
    }

    if (q.next()) {
        const QString result = q.value(0).toString().trimmed().toLower();
        if (result == "ok"_L1) {
            qInfo() << "[DataStore] quick_check passed";
            return true;
        }
        qWarning() << "[DataStore] quick_check FAILED:" << q.value(0).toString();
        return false;
    }

    qWarning() << "[DataStore] quick_check returned no rows";
    return false;
}

// ─── Signal / timer infrastructure ─────────────────────────────────

void DataStore::scheduleDataChangedSignal()
{
    bool expected = false;
    if (!m_reloadInboxScheduled.compare_exchange_strong(expected, true))
        return;

    QTimer::singleShot(300, this, [this]() {
        m_reloadInboxScheduled.store(false);
        notifyDataChanged();
    });
}

void
DataStore::warmStatsCacheThen(std::function<void()> callback) {
    const auto keys = m_folderStats->statsKeysFromFolders();

    m_folderStats->invalidateStatsCache(keys);
    m_folderStats->invalidateTagItemsCache();

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [watcher, cb = std::move(callback)]() {
        watcher->deleteLater();
        cb();
    });
    watcher->setFuture(QtConcurrent::run([this, keys]() {
        for (const auto &key : keys) {
            (void)m_folderStats->statsForFolder(key, {});
        }
        (void)m_folderStats->tagItems();
    }));
}

void DataStore::notifyDataChanged()
{
    warmStatsCacheThen([this]() {
        emit dataChanged();
    });
}

// ─── Folder management ─────────────────────────────────────────────

QVariantList DataStore::folders() const { return m_folderStats->folders(); }
QStringList DataStore::inboxCategoryTabs() const { return m_folderStats->inboxCategoryTabs(); }
QVariantList DataStore::tagItems() const { return m_folderStats->tagItems(); }

void
DataStore::upsertFolder(const QVariantMap &folder) {
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) { return; }
    }

    m_folderStats->upsertFolder(folder);

    if (QThread::currentThread() == thread()) {
        reloadFolders();
    } else {
        bool expected = false;
        if (m_pendingFoldersReload.compare_exchange_strong(expected, true)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_pendingFoldersReload.store(false);
                reloadFolders();
            }, Qt::QueuedConnection);
        }
    }
}

void
DataStore::clearNewMessageCounts(const QString &folderKey) {
    m_folderStats->clearNewMessageCounts(folderKey);
    emit dataChanged();
}

void
DataStore::reloadFolders() {
    m_folderStats->loadFolders();
    warmStatsCacheThen([this]() {
        emit foldersChanged();
    });
}

// ─── Message CRUD forwarding — delegates to m_messages ─────────────

void DataStore::upsertHeaders(const QVariantList &headers) { m_messages->upsertHeaders(headers); }
void DataStore::upsertHeader(const QVariantMap &header) { m_messages->upsertHeader(header); }
void DataStore::pruneFolderToUids(const QString &accountEmail, const QString &folder, const QStringList &uids) { m_messages->pruneFolderToUids(accountEmail, folder, uids); }
void DataStore::removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids, bool skipOrphanCleanup) { m_messages->removeAccountUidsEverywhere(accountEmail, uids, skipOrphanCleanup); }

void DataStore::markMessageRead(const QString &accountEmail, const QString &uid) {
    m_messages->markMessageRead(accountEmail, uid);
    emit messageMarkedRead(accountEmail.trimmed(), uid);
    scheduleDataChangedSignal();
}

void DataStore::reconcileReadFlags(const QString &accountEmail, const QString &folder, const QStringList &readUids) { m_messages->reconcileReadFlags(accountEmail, folder, readUids); }

void DataStore::markMessageFlagged(const QString &accountEmail, const QString &uid, bool flagged) {
    m_messages->markMessageFlagged(accountEmail, uid, flagged);
    emit messageFlaggedChanged(accountEmail.trimmed(), uid, flagged);
    scheduleDataChangedSignal();
}

void DataStore::reconcileFlaggedUids(const QString &accountEmail, const QString &folder, const QStringList &flaggedUids) { m_messages->reconcileFlaggedUids(accountEmail, folder, flaggedUids); }
QVariantMap DataStore::folderMapRowForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const { return m_messages->folderMapRowForEdge(accountEmail, folder, uid); }

void DataStore::deleteSingleFolderEdge(const QString &accountEmail, const QString &folder, const QString &uid) { m_messages->deleteSingleFolderEdge(accountEmail, folder, uid); }
void DataStore::deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder, qint64 messageId) { m_messages->deleteFolderEdgesForMessage(accountEmail, folder, messageId); }
QString DataStore::folderUidForMessageId(const QString &accountEmail, const QString &folder, qint64 messageId) const { return m_messages->folderUidForMessageId(accountEmail, folder, messageId); }
void DataStore::insertFolderEdge(const QString &accountEmail, qint64 messageId, const QString &folder, const QString &uid, int unread) { m_messages->insertFolderEdge(accountEmail, messageId, folder, uid, unread); }
QMap<QString, qint64> DataStore::lookupByMessageIdHeaders(const QString &accountEmail, const QStringList &messageIdHeaders) { return m_messages->lookupByMessageIdHeaders(accountEmail, messageIdHeaders); }
void DataStore::removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId) { m_messages->removeAllEdgesForMessageId(accountEmail, messageId); }
QStringList DataStore::folderUids(const QString &accountEmail, const QString &folder) const { return m_messages->folderUids(accountEmail, folder); }
QStringList DataStore::folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const { return m_messages->folderUidsWithNullSnippet(accountEmail, folder); }
qint64 DataStore::folderMaxUid(const QString &accountEmail, const QString &folder) const { return m_messages->folderMaxUid(accountEmail, folder); }
qint64 DataStore::folderMessageCount(const QString &accountEmail, const QString &folder) const { return m_messages->folderMessageCount(accountEmail, folder); }
QStringList DataStore::bodyFetchCandidates(const QString &accountEmail, const QString &folder, const int limit) const { return m_messages->bodyFetchCandidates(accountEmail, folder, limit); }
QVariantList DataStore::bodyFetchCandidatesByAccount(const QString &accountEmail, const int limit) const { return m_messages->bodyFetchCandidatesByAccount(accountEmail, limit); }
QVariantList DataStore::fetchCandidatesForMessageKey(const QString &accountEmail, const QString &folder, const QString &uid) const { return m_messages->fetchCandidatesForMessageKey(accountEmail, folder, uid); }
QVariantMap DataStore::messageByKey(const QString &accountEmail, const QString &folder, const QString &uid) const { return m_messages->messageByKey(accountEmail, folder, uid); }
QVariantList DataStore::messagesForThread(const QString &accountEmail, const QString &threadId) const { return m_messages->messagesForThread(accountEmail, threadId); }
bool DataStore::hasUsableBodyForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const { return m_messages->hasUsableBodyForEdge(accountEmail, folder, uid); }

bool DataStore::updateBodyForKey(const QString &accountEmail, const QString &folder, const QString &uid, const QString &bodyHtml) {
    const bool changed = m_messages->updateBodyForKey(accountEmail, folder, uid, bodyHtml);
    if (changed) {
        emit bodyHtmlUpdated(accountEmail.trimmed(), folder.trimmed(), uid.trimmed());
    }
    return changed;
}

QVariantList DataStore::messagesForSelection(const QString &folderKey, const QStringList &selectedCategories, int selectedCategoryIndex, int limit, int offset, bool *hasMore) const { return m_messages->messagesForSelection(folderKey, selectedCategories, selectedCategoryIndex, limit, offset, hasMore); }
QVariantList DataStore::groupedMessagesForSelection(const QString &folderKey, const QStringList &selectedCategories, int selectedCategoryIndex, bool todayExpanded, bool yesterdayExpanded, bool lastWeekExpanded, bool twoWeeksAgoExpanded, bool olderExpanded) const { return m_messages->groupedMessagesForSelection(folderKey, selectedCategories, selectedCategoryIndex, todayExpanded, yesterdayExpanded, lastWeekExpanded, twoWeeksAgoExpanded, olderExpanded); }
void DataStore::upsertAttachments(qint64 messageId, const QString &accountEmail, const QVariantList &attachments) { m_messages->upsertAttachments(messageId, accountEmail, attachments); }
QVariantList DataStore::attachmentsForMessage(const QString &accountEmail, const QString &folder, const QString &uid) const { return m_messages->attachmentsForMessage(accountEmail, folder, uid); }
QVariantList DataStore::searchMessages(const QString &query, int limit, int offset, bool *hasMore) const { return m_messages->searchMessages(query, limit, offset, hasMore); }

// ─── Folder stats forwarding to FolderStatsStore ───────────────────

QVariantMap DataStore::folderSyncStatus(const QString &accountEmail, const QString &folder) const { return m_folderStats->folderSyncStatus(accountEmail, folder); }
void DataStore::upsertFolderSyncStatus(const QString &accountEmail, const QString &folder, const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) { m_folderStats->upsertFolderSyncStatus(accountEmail, folder, uidNext, highestModSeq, messages); }
qint64 DataStore::folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const { return m_folderStats->folderLastSyncModSeq(accountEmail, folder); }
void DataStore::updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder, const qint64 modseq) { m_folderStats->updateFolderLastSyncModSeq(accountEmail, folder, modseq); }

// ─── Contact / avatar forwarding to ContactStore ───────────────────

QString DataStore::avatarForEmail(const QString &email) const { return m_contacts->avatarForEmail(email); }
QString DataStore::displayNameForEmail(const QString &email) const { return m_contacts->displayNameForEmail(email); }
QString DataStore::preferredSelfDisplayName(const QString &accountEmail) const { return m_contacts->preferredSelfDisplayName(accountEmail); }
bool DataStore::avatarShouldRefresh(const QString &email, int ttlSeconds, int maxFailures) const { return m_contacts->avatarShouldRefresh(email, ttlSeconds, maxFailures); }
QStringList DataStore::staleGooglePeopleEmails(int limit) const { return m_contacts->staleGooglePeopleEmails(limit); }
void DataStore::updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source) { m_contacts->updateContactAvatar(email, avatarUrl, source); }
QVariantList DataStore::searchContacts(const QString &prefix, int limit) const { return m_contacts->searchContacts(prefix, limit); }

// ─── User prefs forwarding to UserPrefsStore ───────────────────────

QVariantMap DataStore::migrationStats() const { return m_prefs->migrationStats(); }
bool DataStore::isSenderTrusted(const QString &domain) const { return m_prefs->isSenderTrusted(domain); }
void DataStore::setTrustedSenderDomain(const QString &domain) { m_prefs->setTrustedSenderDomain(domain); }
QVariantList DataStore::favoritesConfig() const { return m_prefs->favoritesConfig(); }
QVariantList DataStore::recentSearches(int limit) const { return m_prefs->recentSearches(limit); }
void DataStore::addRecentSearch(const QString &query) { m_prefs->addRecentSearch(query); }
void DataStore::removeRecentSearch(const QString &query) { m_prefs->removeRecentSearch(query); }

bool DataStore::hasCachedHeadersForFolder(const QString &rawFolderName, int minCount) const { return m_folderStats->hasCachedHeadersForFolder(rawFolderName, minCount); }
QVariantMap DataStore::statsForFolder(const QString &folderKey, const QString &rawFolderName) const { return m_folderStats->statsForFolder(folderKey, rawFolderName); }
int DataStore::newMessageCount(const QString &folderKey) const { return m_folderStats->newMessageCount(folderKey); }
int DataStore::inboxCount() const { return m_folderStats->inboxCount(); }

void
DataStore::setFavoriteEnabled(const QString &key, bool enabled) {
    m_prefs->setFavoriteEnabled(key, enabled);
    emit favoritesConfigChanged();
}

QVariantList DataStore::userFolders() const { return m_prefs->userFolders(); }

bool
DataStore::createUserFolder(const QString &name) {
    if (!m_prefs->createUserFolder(name)) { return false; }

    emit userFoldersChanged();
    return true;
}

bool
DataStore::deleteUserFolder(const QString &name) {
    if (!m_prefs->deleteUserFolder(name)) { return false; }

    emit userFoldersChanged();
    return true;
}
