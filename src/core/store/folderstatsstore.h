#pragma once

#include <QHash>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

/// Owns folder list, stats caching, tag aggregation, sync status tracking,
/// and new-message counters. Thread-safe via per-thread SQLite connections.
class FolderStatsStore
{
public:
    using DbAccessor = std::function<QSqlDatabase()>;

    explicit FolderStatsStore(DbAccessor dbAccessor);

    // ── Folder list ──────────────────────────────────────────────
    QVariantList folders() const;
    void loadFolders();
    void upsertFolder(const QVariantMap &folder) const;

    // ── Stats queries (thread-safe, cache-backed) ────────────────
    QVariantMap statsForFolder(const QString &folderKey, const QString &rawFolderName) const;
    QStringList statsKeysFromFolders() const;
    void invalidateStatsCache(const QStringList &keys) const;
    void invalidateTagItemsCache() const;

    // ── Tags ─────────────────────────────────────────────────────
    QVariantList tagItems() const;
    QStringList inboxCategoryTabs() const;

    // ── Inbox / header counts ────────────────────────────────────
    qint32 inboxCount() const;
    bool hasCachedHeadersForFolder(const QString &rawFolderName, qint32 minCount = 60) const;

    // ── New-message counters (in-memory) ─────────────────────────
    qint32 newMessageCount(const QString &folderKey) const;
    void clearNewMessageCounts(const QString &folderKey);
    void incrementNewMessageCount(const QString &rawFolder);

    // ── Sync status CRUD ─────────────────────────────────────────
    QVariantMap folderSyncStatus(const QString &accountEmail, const QString &folder) const;
    void upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                qint64 uidNext, qint64 highestModSeq, qint64 messages) const;
    qint64 folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const;
    void updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder,
                                    qint64 modseq) const;

    // ── Shared label classification helpers ──────────────────────
    static bool isSystemLabelName(const QString &label);
    static bool isCategoryFolderName(const QString &folder);

private:
    DbAccessor m_db;

    QVariantList m_folders;

    mutable QMutex m_folderStatsCacheMutex;
    mutable QHash<QString, QVariantMap> m_folderStatsCache;

    mutable QMutex m_tagItemsCacheMutex;
    mutable QVariantList m_tagItemsCache;
    mutable bool m_tagItemsCacheValid{false};

    mutable QMutex m_newCountMutex;
    QHash<QString, qint32> m_newMessageCounts;
};
