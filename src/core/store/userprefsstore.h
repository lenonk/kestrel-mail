#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

/// Owns small CRUD features for user preferences and local configuration:
/// favorites sidebar, user folders, recent searches, sender trust, migration stats.
class UserPrefsStore
{
public:
    using DbAccessor = std::function<QSqlDatabase()>;

    explicit UserPrefsStore(DbAccessor dbAccessor);

    // ── Favorites sidebar config ─────────────────────────────────
    QVariantList favoritesConfig() const;
    void setFavoriteEnabled(const QString &key, bool enabled) const;

    // ── User-created local folders ───────────────────────────────
    QVariantList userFolders() const;
    bool createUserFolder(const QString &name) const;
    bool deleteUserFolder(const QString &name) const;

    // ── Recent searches ──────────────────────────────────────────
    QVariantList recentSearches(qint32 limit = 5) const;
    void addRecentSearch(const QString &query) const;
    void removeRecentSearch(const QString &query) const;

    // ── Sender image trust ───────────────────────────────────────
    bool isSenderTrusted(const QString &domain) const;
    void setTrustedSenderDomain(const QString &domain) const;

    // ── Diagnostics ──────────────────────────────────────────────
    QVariantMap migrationStats() const;

private:
    DbAccessor m_db;
};
