#include "userprefsstore.h"

#include <QSet>
#include <QSqlQuery>

using namespace Qt::Literals::StringLiterals;

// ─── Construction ────────────────────────────────────────────────

UserPrefsStore::UserPrefsStore(DbAccessor dbAccessor)
    : m_db(std::move(dbAccessor)) {
}

// ─── Favorites sidebar config ────────────────────────────────────

QVariantList
UserPrefsStore::favoritesConfig() const {
    struct FavDef { QString key; QString name; };
    static const QList<FavDef> kDefs = {
        {"all-inboxes"_L1, "All Inboxes"_L1},
        {"unread"_L1,      "Unread"_L1     },
        {"flagged"_L1,     "Flagged"_L1    },
        {"outbox"_L1,      "Outbox"_L1     },
        {"sent"_L1,        "Sent"_L1       },
        {"trash"_L1,       "Trash"_L1      },
        {"drafts"_L1,      "Drafts"_L1     },
        {"junk"_L1,        "Junk Email"_L1 },
        {"archive"_L1,     "Archive"_L1    },
        {"unreplied"_L1,   "Unreplied"_L1  },
        {"snoozed"_L1,     "Snoozed"_L1   },
    };
    static const QSet<QString> kDefaultEnabled{"all-inboxes"_L1, "unread"_L1, "flagged"_L1};

    QSqlQuery q(m_db());

    q.exec("SELECT key, enabled FROM favorites_config"_L1);

    QHash<QString, bool> enabledMap;
    while (q.next()) {
        enabledMap.insert(q.value(0).toString(), q.value(1).toBool());
    }

    QVariantList out;
    for (const auto &[key, name] : kDefs) {
        const auto en = enabledMap.contains(key)
                        ? enabledMap[key]
                        : kDefaultEnabled.contains(key);
        QVariantMap m;
        m["key"_L1]     = key;
        m["name"_L1]    = name;
        m["enabled"_L1] = en;
        out << m;
    }

    return out;
}

void
UserPrefsStore::setFavoriteEnabled(const QString &key, const bool enabled) const {
    QSqlQuery q(m_db());
    q.prepare("INSERT INTO favorites_config(key,enabled) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET enabled=excluded.enabled"_L1);

    q.addBindValue(key);
    q.addBindValue(enabled ? 1 : 0);
    q.exec();
}

// ─── User-created local folders ──────────────────────────────────

QVariantList
UserPrefsStore::userFolders() const {
    QSqlQuery q(m_db());

    q.exec("SELECT name FROM user_folders ORDER BY sort_order ASC, name ASC"_L1);

    QVariantList out;
    while (q.next()) {
        QVariantMap m;
        m["name"_L1] = q.value(0).toString();
        out << m;
    }

    return out;
}

bool
UserPrefsStore::createUserFolder(const QString &name) const {
    const auto n = name.trimmed();
    if (n.isEmpty()) { return false; }

    QSqlQuery q(m_db());
    q.prepare("INSERT OR IGNORE INTO user_folders (name) VALUES (?)"_L1);

    q.addBindValue(n);

    return q.exec() && q.numRowsAffected() >= 1;
}

bool
UserPrefsStore::deleteUserFolder(const QString &name) const {
    QSqlQuery q(m_db());
    q.prepare("DELETE FROM user_folders WHERE name = ?"_L1);

    q.addBindValue(name.trimmed());

    return q.exec() && q.numRowsAffected() >= 1;
}

// ─── Recent searches ─────────────────────────────────────────────

QVariantList
UserPrefsStore::recentSearches(const qint32 limit) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    QSqlQuery q(database);
    q.prepare("SELECT query, searched_at FROM search_history ORDER BY searched_at DESC LIMIT :limit"_L1);

    q.bindValue(":limit"_L1, limit);

    QVariantList out;
    if (q.exec()) {
        while (q.next()) {
            QVariantMap row;
            row.insert("query"_L1,      q.value(0).toString());
            row.insert("searchedAt"_L1, q.value(1).toString());
            out.push_back(row);
        }
    }

    return out;
}

void
UserPrefsStore::addRecentSearch(const QString &query) const {
    const auto term = query.trimmed();
    if (term.isEmpty()) { return; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare("INSERT INTO search_history (query, searched_at) VALUES (:q, datetime('now')) ON CONFLICT(query) DO UPDATE SET searched_at = datetime('now')"_L1);

    q.bindValue(":q"_L1, term);
    q.exec();
}

void
UserPrefsStore::removeRecentSearch(const QString &query) const {
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare("DELETE FROM search_history WHERE query = :q"_L1);

    q.bindValue(":q"_L1, query.trimmed());
    q.exec();
}

// ─── Sender image trust ──────────────────────────────────────────

bool
UserPrefsStore::isSenderTrusted(const QString &domain) const {
    if (domain.trimmed().isEmpty()) { return false; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return false; }

    QSqlQuery q(database);
    q.prepare("SELECT 1 FROM sender_image_permissions WHERE domain=:domain LIMIT 1"_L1);

    q.bindValue(":domain"_L1, domain.trimmed().toLower());

    return q.exec() && q.next();
}

void
UserPrefsStore::setTrustedSenderDomain(const QString &domain) const {
    const auto d = domain.trimmed().toLower();
    if (d.isEmpty()) { return; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare("INSERT INTO sender_image_permissions (domain) VALUES (:domain) ON CONFLICT(domain) DO NOTHING"_L1);

    q.bindValue(":domain"_L1, d);
    q.exec();
}

// ─── Diagnostics ─────────────────────────────────────────────────

QVariantMap
UserPrefsStore::migrationStats() const {
    QVariantMap out;

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

    auto scalar = [&](const QString &sql) -> qint32 {
        QSqlQuery q(database);
        if (!q.exec(sql) || !q.next()) { return -1; }

        return q.value(0).toInt();
    };

    out.insert("messages"_L1, scalar("SELECT count(*) FROM messages"_L1));
    out.insert("folderEdges"_L1, scalar("SELECT count(*) FROM message_folder_map"_L1));
    out.insert("labels"_L1, scalar("SELECT count(*) FROM message_labels"_L1));
    out.insert("tags"_L1, scalar("SELECT count(*) FROM tags"_L1));
    out.insert("tagMaps"_L1, scalar("SELECT count(*) FROM message_tag_map"_L1));

    out.insert("labelsMissingEdge"_L1, scalar(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )"_L1));

    out.insert("edgesMissingLabel"_L1, scalar(R"(
        SELECT count(*)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
    )"_L1));

    out.insert("labelsMissingTagMap"_L1, scalar(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE NOT EXISTS (
            SELECT 1
            FROM tags t
            JOIN message_tag_map mtm ON mtm.tag_id = t.id
            WHERE t.normalized_name = lower(ml.label)
              AND mtm.account_email = ml.account_email
              AND mtm.message_id = ml.message_id
        )
    )"_L1));

    return out;
}
