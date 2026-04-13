#include "folderstatsstore.h"
#include "folderkey.h"

#include <QColor>
#include <QSqlQuery>

using namespace Qt::Literals::StringLiterals;

namespace {

struct ThreadCounts {
    qint32 total = 0;
    qint32 unread = 0;
};

/// Runs thread-deduplicated total and unread counts for a given folder WHERE clause.
/// The WHERE clause should filter `message_folder_map mfm` rows.
/// Bind params in `binds` are applied to both queries.
ThreadCounts
queryThreadStats(const QSqlDatabase &database, const QString &whereClause,
                 const QList<std::pair<QString, QVariant>> &binds = {}) {
    ThreadCounts result;

    // Thread-total count.
    QSqlQuery qTotal(database);
    qTotal.prepare(
        "SELECT COUNT(*) FROM ("
        "  SELECT mfm.account_email,"
        "         COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key"
        "  FROM message_folder_map mfm"
        "  JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email"
        "  WHERE %1"
        "  GROUP BY mfm.account_email, thread_key"
        ")"_L1.arg(whereClause));

    for (const auto &[name, value] : binds) { qTotal.bindValue(name, value); }
    if (qTotal.exec() && qTotal.next()) { result.total = qTotal.value(0).toInt(); }

    // Thread-unread count (same WHERE + unread filter).
    QSqlQuery qUnread(database);
    qUnread.prepare(
        "SELECT COUNT(*) FROM ("
        "  SELECT mfm.account_email,"
        "         COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key"
        "  FROM message_folder_map mfm"
        "  JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email"
        "  WHERE %1"
        "    AND EXISTS ("
        "      SELECT 1 FROM message_folder_map x"
        "      WHERE x.account_email=mfm.account_email"
        "        AND x.message_id=mfm.message_id"
        "        AND x.unread=1"
        "    )"
        "  GROUP BY mfm.account_email, thread_key"
        ")"_L1.arg(whereClause));

    for (const auto &[name, value] : binds) { qUnread.bindValue(name, value); }
    if (qUnread.exec() && qUnread.next()) { result.unread = qUnread.value(0).toInt(); }

    return result;
}

// WHERE clause fragments shared across statsForFolder branches.
constexpr QLatin1StringView kWhereInbox {
    "lower(mfm.folder)='inbox'"
    " OR lower(mfm.folder)='[gmail]/inbox'"
    " OR lower(mfm.folder)='[google mail]/inbox'"
    " OR lower(mfm.folder) LIKE '%/inbox'"
};

constexpr QLatin1StringView kWhereTrashExclude {
    "NOT EXISTS ("
    "  SELECT 1 FROM message_folder_map t"
    "  WHERE t.account_email=mfm.account_email"
    "    AND t.message_id=mfm.message_id"
    "    AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash'"
    "         OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')"
    ")"
};

constexpr QLatin1StringView kWhereInboxWithCategories {
    "lower(mfm.folder) IN ("
    "  'inbox',"
    "  '[gmail]/categories/primary','[gmail]/categories/promotions',"
    "  '[gmail]/categories/social','[gmail]/categories/updates',"
    "  '[gmail]/categories/forums','[gmail]/categories/purchases',"
    "  '[google mail]/categories/primary','[google mail]/categories/promotions',"
    "  '[google mail]/categories/social','[google mail]/categories/updates',"
    "  '[google mail]/categories/forums','[google mail]/categories/purchases'"
    ")"
};

} // namespace

// ─── Construction ────────────────────────────────────────────────

FolderStatsStore::FolderStatsStore(DbAccessor dbAccessor)
    : m_db(std::move(dbAccessor)) {
}

// ─── Static label helpers ────────────────────────────────────────

bool
FolderStatsStore::isSystemLabelName(const QString &label) {
    const auto l = label.trimmed().toLower();
    if (l.isEmpty()) { return true; }

    return l == "inbox"_L1
            || l == "sent"_L1
            || l == "sent mail"_L1
            || l == "draft"_L1
            || l == "drafts"_L1
            || l == "trash"_L1
            || l == "spam"_L1
            || l == "junk"_L1
            || l == "all mail"_L1
            || l == "starred"_L1
            || l == "[gmail]"_L1
            || l == "[google mail]"_L1
            || l.startsWith("[gmail]/"_L1)
            || l.startsWith("[google mail]/"_L1)
            || l.startsWith("\\"_L1);
}

bool
FolderStatsStore::isCategoryFolderName(const QString &folder) {
    return folder.trimmed().toLower().contains("/categories/"_L1);
}

// ─── Folder list ─────────────────────────────────────────────────

QVariantList
FolderStatsStore::folders() const {
    return m_folders;
}

void
FolderStatsStore::loadFolders() {
    m_folders.clear();

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.exec(R"(
      SELECT account_email, name, flags, special_use
      FROM folders
      ORDER BY
        CASE
          WHEN UPPER(name)='INBOX' THEN 0
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/PRIMARY' THEN 1
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/PROMOTIONS' THEN 2
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/SOCIAL' THEN 3
          ELSE 10
        END,
        name COLLATE NOCASE
    )"_L1);

    while (q.next()) {
        QVariantMap row;
        row.insert("accountEmail"_L1, q.value(0));
        row.insert("name"_L1, q.value(1));
        row.insert("flags"_L1, q.value(2));
        row.insert("specialUse"_L1, q.value(3));
        m_folders.push_back(row);
    }
}

void
FolderStatsStore::upsertFolder(const QVariantMap &folder) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO folders (account_email, name, flags, special_use)
        VALUES (:account_email, :name, :flags, :special_use)
        ON CONFLICT(account_email, name) DO UPDATE SET
          flags = excluded.flags,
          special_use = excluded.special_use
    )"_L1);

    q.bindValue(":account_email"_L1, folder.value("accountEmail"_L1));
    q.bindValue(":name"_L1, folder.value("name"_L1));
    q.bindValue(":flags"_L1, folder.value("flags"_L1));
    q.bindValue(":special_use"_L1, folder.value("specialUse"_L1));
    q.exec();
}

// ─── Stats queries ───────────────────────────────────────────────

QStringList
FolderStatsStore::statsKeysFromFolders() const {
    QStringList keys;
    keys << "favorites:all-inboxes"_L1
         << "favorites:unread"_L1
         << "favorites:flagged"_L1;

    for (const auto &fv : m_folders) {
        const auto f = fv.toMap();
        const auto rawName = f.value("name"_L1).toString().trimmed();
        if (rawName.isEmpty()) { continue; }
        if (rawName.contains("/Categories/"_L1, Qt::CaseInsensitive)) { continue; }

        auto norm = rawName.toLower();
        if (norm.startsWith("[google mail]/"_L1)) {
            norm = "[gmail]/"_L1 + norm.mid(14);
        }

        const auto email = f.value("accountEmail"_L1).toString().trimmed().toLower();
        keys << (email.isEmpty() ? ("account:"_L1 + norm) : ("account:"_L1 + email + ":"_L1 + norm));
        if (f.value("specialUse"_L1).toString().trimmed().isEmpty()
                && !norm.contains(QLatin1Char('/'))) {
            keys << ("tag:"_L1 + norm);
        }
    }

    keys.removeDuplicates();

    return keys;
}

void
FolderStatsStore::invalidateStatsCache(const QStringList &keys) const {
    QMutexLocker lock(&m_folderStatsCacheMutex);
    for (const auto &k : keys) {
        m_folderStatsCache.remove(k);
    }
}

void
FolderStatsStore::invalidateTagItemsCache() const {
    QMutexLocker lock(&m_tagItemsCacheMutex);
    m_tagItemsCacheValid = false;
}

QVariantMap
FolderStatsStore::statsForFolder(const QString &folderKey, const QString &rawFolderName) const {
    QVariantMap out;
    const auto key = folderKey.trimmed().toLower();

    // Fast path: return cached result (pre-warmed on worker thread before foldersChanged).
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        if (const auto it = m_folderStatsCache.constFind(key); it != m_folderStatsCache.cend()) { return *it; }
    }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        out.insert("total"_L1, 0);
        out.insert("unread"_L1, 0);

        return out;
    }

    ThreadCounts counts;

    if (key == "favorites:all-inboxes"_L1 || key == "favorites:unread"_L1) {
        const auto unreadOnly = (key == "favorites:unread"_L1);
        const auto where = "(%1) AND (:unread_only=0 OR EXISTS ("
            "SELECT 1 FROM message_folder_map u WHERE u.account_email=mfm.account_email"
            " AND u.message_id=mfm.message_id AND u.unread=1)) AND %2"_L1
            .arg(kWhereInbox, kWhereTrashExclude);

        counts = queryThreadStats(database, where, {{":unread_only"_L1, unreadOnly ? 1 : 0}});
        if (unreadOnly) { counts.total = counts.unread; }

    } else if (key == "favorites:flagged"_L1 || key.startsWith("local:"_L1)) {
        // No stats for flagged/local yet.

    } else if (key.startsWith("tag:"_L1)) {
        if (const auto tag = key.mid("tag:"_L1.size()).trimmed().toLower(); !tag.isEmpty()) {
            if (tag == "important"_L1) {
                counts = queryThreadStats(database, "lower(mfm.folder) LIKE '%/important'"_L1);
            } else {
                counts = queryThreadStats(database, "lower(mfm.folder)=:folder"_L1,
                                          {{":folder"_L1, tag}});
            }
        }

    } else {
        auto folder = rawFolderName.trimmed().toLower();
        QString accountEmail;
        if (key.startsWith("account:"_L1)) {
            const auto parsed = FolderKey::parseAccountKey(key);
            accountEmail = parsed.accountEmail;
            if (folder.isEmpty()) folder = parsed.folder;
        }

        const auto kAccountWhere = accountEmail.isEmpty()
            ? QString()
            : " AND lower(mfm.account_email)=:account_email"_L1;
        QList<QPair<QString, QVariant>> binds;
        if (!accountEmail.isEmpty())
            binds << QPair(":account_email"_L1, accountEmail);

        if (!folder.isEmpty()) {
            if (folder == "inbox"_L1) {
                counts = queryThreadStats(database, QString(kWhereInboxWithCategories) + kAccountWhere, binds);
            } else {
                binds << QPair(":folder"_L1, folder);
                counts = queryThreadStats(database, "lower(mfm.folder)=:folder"_L1 + kAccountWhere, binds);
            }
        }
    }

    out.insert("total"_L1, counts.total);
    out.insert("unread"_L1, counts.unread);
    out.insert("newMessages"_L1, newMessageCount(folderKey));

    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        m_folderStatsCache.insert(key, out);
    }

    return out;
}

// ─── Tags ────────────────────────────────────────────────────────

QStringList
FolderStatsStore::inboxCategoryTabs() const {
    return { "Primary"_L1, "Promotions"_L1, "Social"_L1 };
}

QVariantList
FolderStatsStore::tagItems() const {
    {
        QMutexLocker lock(&m_tagItemsCacheMutex);
        if (m_tagItemsCacheValid) { return m_tagItemsCache; }
    }

    QVariantList out;
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

    QHash<QString, QVariantMap> byLabel;

    QSqlQuery q(database);
    q.prepare(R"(
        SELECT t.normalized_name AS label,
               COALESCE(NULLIF(trim(t.name), ''), t.normalized_name) AS display_name,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT mtm2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_tag_map mtm2
                   JOIN messages cm2 ON cm2.id=mtm2.message_id AND cm2.account_email=mtm2.account_email
                   WHERE mtm2.tag_id=t.id
                   GROUP BY mtm2.account_email, thread_key
                 )
               ) AS total,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT mtm2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_tag_map mtm2
                   JOIN messages cm2 ON cm2.id=mtm2.message_id AND cm2.account_email=mtm2.account_email
                   WHERE mtm2.tag_id=t.id
                     AND EXISTS (
                       SELECT 1 FROM message_folder_map mfu
                       WHERE mfu.account_email=mtm2.account_email
                         AND mfu.message_id=mtm2.message_id
                         AND mfu.unread=1
                     )
                   GROUP BY mtm2.account_email, thread_key
                 )
               ) AS unread,
               COALESCE(t.color, '') AS color
        FROM tags t
        LEFT JOIN message_tag_map mtm ON mtm.tag_id=t.id
        GROUP BY t.id
        ORDER BY t.normalized_name
    )"_L1);

    if (q.exec()) {
        while (q.next()) {
            const auto label = q.value(0).toString().trimmed();
            if (isSystemLabelName(label) || isCategoryFolderName(label)) { continue; }

            QVariantMap row;
            row.insert("label"_L1, label);
            row.insert("name"_L1, q.value(1).toString().trimmed());
            row.insert("total"_L1, q.value(2).toInt());
            row.insert("unread"_L1, q.value(3).toInt());
            row.insert("color"_L1, q.value(4).toString().trimmed());
            byLabel.insert(label, row);
        }
    }

    // Add Important as a tag (folder-backed, not label-backed).
    QSqlQuery qImportant(database);
    qImportant.prepare(R"(
        SELECT (
                 SELECT COUNT(*)
                 FROM (
                   SELECT m2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_folder_map m2
                   JOIN messages cm2 ON cm2.id=m2.message_id AND cm2.account_email=m2.account_email
                   WHERE lower(m2.folder) LIKE '%/important'
                   GROUP BY m2.account_email, thread_key
                 )
               ) AS total,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT m2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_folder_map m2
                   JOIN messages cm2 ON cm2.id=m2.message_id AND cm2.account_email=m2.account_email
                   WHERE lower(m2.folder) LIKE '%/important'
                     AND EXISTS (
                       SELECT 1 FROM message_folder_map u
                       WHERE u.account_email=m2.account_email
                         AND u.message_id=m2.message_id
                         AND u.unread=1
                     )
                   GROUP BY m2.account_email, thread_key
                 )
               ) AS unread
    )"_L1);

    {
        QVariantMap row;
        row.insert("label"_L1, "important"_L1);
        row.insert("name"_L1, "Important"_L1);
        if (qImportant.exec() && qImportant.next()) {
            row.insert("total"_L1, qImportant.value(0).toInt());
            row.insert("unread"_L1, qImportant.value(1).toInt());
        } else {
            row.insert("total"_L1, 0);
            row.insert("unread"_L1, 0);
        }
        row.insert("color"_L1, QString());
        byLabel.insert("important"_L1, row);
    }

    // Fallback/union with top-level custom folders.
    QSqlQuery qFolders(database);
    qFolders.prepare("SELECT lower(name), lower(flags) FROM folders"_L1);

    if (qFolders.exec()) {
        while (qFolders.next()) {
            const auto name = qFolders.value(0).toString().trimmed();
            if (name.isEmpty()) { continue; }
            if (name == "[google mail]"_L1) { continue; }
            if (name.contains('/')) { continue; }
            if (isSystemLabelName(name) || isCategoryFolderName(name)) { continue; }
            if (byLabel.contains(name)) { continue; }

            QVariantMap row;
            row.insert("label"_L1, name);
            row.insert("name"_L1, name);
            row.insert("total"_L1, 0);
            row.insert("unread"_L1, 0);
            byLabel.insert(name, row);
        }
    }

    auto keys = byLabel.keys();
    std::ranges::sort(keys);

    for (qint32 i = 0; i < keys.size(); ++i) {
        const auto &k = keys.at(i);
        auto row = byLabel.value(k);

        auto color = row.value("color"_L1).toString().trimmed();
        if (color.isEmpty()) {
            const auto hue = (i * 137) % 360;
            QColor c;
            c.setHsv(hue, 190, 225);
            color = c.name(QColor::HexRgb);

            QSqlQuery qColor(database);
            qColor.prepare("UPDATE tags SET color=:color, updated_at=datetime('now') WHERE normalized_name=:name"_L1);

            qColor.bindValue(":color"_L1, color);
            qColor.bindValue(":name"_L1, k);
            qColor.exec();
        }

        row.insert("color"_L1, color);
        out.push_back(row);
    }

    {
        QMutexLocker lock(&m_tagItemsCacheMutex);
        m_tagItemsCache = out;
        m_tagItemsCacheValid = true;
    }

    return out;
}

// ─── Inbox / header counts ───────────────────────────────────────

qint32
FolderStatsStore::inboxCount() const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return 0; }

    QSqlQuery q(database);
    q.exec("SELECT COUNT(*) FROM message_folder_map WHERE lower(folder)='inbox'"_L1);

    return (q.next()) ? q.value(0).toInt() : 0;
}

bool
FolderStatsStore::hasCachedHeadersForFolder(const QString &rawFolderName, const qint32 minCount) const {
    const auto folder = rawFolderName.trimmed().toLower();
    if (folder.isEmpty()) { return false; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return false; }

    QSqlQuery q(database);
    q.prepare(R"(
        SELECT COUNT(DISTINCT mfm.message_id)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder)=:folder
    )"_L1);

    q.bindValue(":folder"_L1, folder);
    if (!q.exec() || !q.next()) { return false; }

    return q.value(0).toInt() >= qMax(1, minCount);
}

// ─── New-message counters ────────────────────────────────────────

void
FolderStatsStore::incrementNewMessageCount(const QString &rawFolder) {
    const auto key = rawFolder.trimmed().toLower();
    QMutexLocker lock(&m_newCountMutex);
    m_newMessageCounts[key] += 1;
}

qint32
FolderStatsStore::newMessageCount(const QString &folderKey) const {
    QMutexLocker lock(&m_newCountMutex);
    const auto key = folderKey.trimmed().toLower();

    if (key == "account:inbox"_L1 || key == "favorites:all-inboxes"_L1) {
        return m_newMessageCounts.value("inbox"_L1, 0);
    }

    if (key.startsWith("account:"_L1)) {
        return m_newMessageCounts.value(key.mid(8), 0);
    }

    if (key.startsWith("tag:"_L1)) {
        const auto tag = key.mid(4);
        if (tag == "important"_L1) {
            qint32 sum = 0;
            for (auto it = m_newMessageCounts.cbegin(); it != m_newMessageCounts.cend(); ++it) {
                if (it.key().endsWith("/important"_L1)) { sum += it.value(); }
            }

            return sum;
        }

        return m_newMessageCounts.value(tag, 0);
    }

    return m_newMessageCounts.value(key, 0);
}

void
FolderStatsStore::clearNewMessageCounts(const QString &folderKey) {
    {
        QMutexLocker lock(&m_newCountMutex);

        if (const auto key = folderKey.trimmed().toLower(); key == "account:inbox"_L1 || key == "favorites:all-inboxes"_L1) {
            m_newMessageCounts.remove("inbox"_L1);
            auto it = m_newMessageCounts.begin();
            while (it != m_newMessageCounts.end()) {
                if (it.key().contains("/categories/"_L1)) {
                    it = m_newMessageCounts.erase(it);
                } else {
                    ++it;
                }
            }
        } else if (key.startsWith("account:"_L1)) {
            m_newMessageCounts.remove(key.mid(8));
        } else if (key.startsWith("tag:"_L1)) {
            if (const auto tag = key.mid(4); tag == "important"_L1) {
                auto it = m_newMessageCounts.begin();
                while (it != m_newMessageCounts.end()) {
                    if (it.key().endsWith("/important"_L1)) {
                        it = m_newMessageCounts.erase(it);
                    } else {
                        ++it;
                    }
                }
            } else {
                m_newMessageCounts.remove(tag);
            }
        } else {
            m_newMessageCounts.remove(key);
        }
    }

    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        m_folderStatsCache.clear();
    }
}

// ─── Sync status CRUD ────────────────────────────────────────────

QVariantMap
FolderStatsStore::folderSyncStatus(const QString &accountEmail, const QString &folder) const {
    QVariantMap out;

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

    QSqlQuery q(database);
    q.prepare(R"(
        SELECT uid_next, highest_modseq, messages
        FROM folder_sync_status
        WHERE account_email=:account_email
          AND lower(folder)=lower(:folder)
        LIMIT 1
    )"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec() || !q.next()) { return out; }

    out.insert("uidNext"_L1, q.value(0).toLongLong());
    out.insert("highestModSeq"_L1, q.value(1).toLongLong());
    out.insert("messages"_L1, q.value(2).toLongLong());

    return out;
}

void
FolderStatsStore::upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                          const qint64 uidNext, const qint64 highestModSeq, const qint64 messages) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO folder_sync_status(account_email, folder, uid_next, highest_modseq, messages, updated_at)
        VALUES(:account_email, :folder, :uid_next, :highest_modseq, :messages, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          uid_next=excluded.uid_next,
          highest_modseq=excluded.highest_modseq,
          messages=excluded.messages,
          updated_at=datetime('now')
    )"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":uid_next"_L1, uidNext);
    q.bindValue(":highest_modseq"_L1, highestModSeq);
    q.bindValue(":messages"_L1, messages);
    q.exec();
}

qint64
FolderStatsStore::folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return 0; }

    QSqlQuery q(database);
    q.prepare(
        "SELECT last_sync_modseq FROM folder_sync_status "
        "WHERE account_email=:account_email AND lower(folder)=lower(:folder) LIMIT 1"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec() || !q.next()) { return 0; }

    bool ok = false;
    const auto v = q.value(0).toLongLong(&ok);

    return ok ? v : 0;
}

void
FolderStatsStore::updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder,
                                              const qint64 modseq) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO folder_sync_status(account_email, folder, last_sync_modseq, updated_at)
        VALUES(:account_email, :folder, :modseq, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          last_sync_modseq=excluded.last_sync_modseq,
          updated_at=datetime('now')
    )"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":modseq"_L1, modseq);
    q.exec();
}
