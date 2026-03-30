#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QStringList>

#include <atomic>
#include <functional>

class QSqlDatabase;

class DataStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QStringList inboxCategoryTabs READ inboxCategoryTabs NOTIFY dataChanged)
public:
    explicit DataStore(QObject *parent = nullptr);
    ~DataStore() override;

    QVariantList folders() const;

    Q_INVOKABLE bool init();
    bool quickCheck() const;
    Q_INVOKABLE void upsertHeader(const QVariantMap &header);
    Q_INVOKABLE void upsertHeaders(const QVariantList &headers);
    Q_INVOKABLE void upsertFolder(const QVariantMap &folder);
    Q_INVOKABLE void pruneFolderToUids(const QString &accountEmail, const QString &folder, const QStringList &uids);
    Q_INVOKABLE void removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids,
                                                   bool skipOrphanCleanup = false);
    // Batch flag reconciliation: update unread=0 for UIDs confirmed read on the server.
    // Only marks messages read (never marks unread — respects local mark-as-read state).
    void reconcileReadFlags(const QString &accountEmail, const QString &folder,
                            const QStringList &readUids);
    Q_INVOKABLE void markMessageRead(const QString &accountEmail, const QString &uid);
    // Sets or clears the \Flagged state for a message identified by any uid.
    void markMessageFlagged(const QString &accountEmail, const QString &uid, bool flagged);
    // Batch: marks messages.flagged=1 for UIDs the server confirms are flagged.
    void reconcileFlaggedUids(const QString &accountEmail, const QString &folder,
                              const QStringList &flaggedUids);
    // Returns {messageId (qint64), unread (int)} for a specific folder/uid edge.
    QVariantMap folderMapRowForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const;
    // Deletes the specific (folder, uid) edge, cleans orphaned messages, reloads inbox.
    void deleteSingleFolderEdge(const QString &accountEmail, const QString &folder, const QString &uid);
    // Deletes all (folder, message_id) edges (cross-folder safe — no uid needed).
    void deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder, qint64 messageId);
    // Returns the UID for a message_id in a specific folder (empty string if not found).
    QString folderUidForMessageId(const QString &accountEmail, const QString &folder, qint64 messageId) const;
    // Inserts a new (folder, uid) edge for an already-known message_id (used after UID MOVE COPYUID).
    void insertFolderEdge(const QString &accountEmail, qint64 messageId, const QString &folder, const QString &uid, int unread);
    // Cross-folder dedup: returns {message_id_header → messages.id} for any of the given
    // Message-ID header values already stored for this account.
    QMap<QString, qint64> lookupByMessageIdHeaders(const QString &accountEmail, const QStringList &messageIdHeaders);
    // Removes ALL folder edges for a message_id (fallback when server gives no COPYUID).
    void removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId);
    Q_INVOKABLE QStringList folderUids(const QString &accountEmail, const QString &folder) const;
    QStringList folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMaxUid(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMessageCount(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE QVariantMap folderSyncStatus(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE void upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                            qint64 uidNext, qint64 highestModSeq, qint64 messages);
    Q_INVOKABLE qint64 folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE void updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder,
                                                qint64 modseq);
    Q_INVOKABLE QStringList bodyFetchCandidates(const QString &accountEmail, const QString &folder,
                                                int limit = 10) const;
    Q_INVOKABLE QVariantList bodyFetchCandidatesByAccount(const QString &accountEmail,
                                                          int limit = 10) const;
    Q_INVOKABLE QVariantList fetchCandidatesForMessageKey(const QString &accountEmail,
                                                           const QString &folder,
                                                           const QString &uid) const;
    Q_INVOKABLE QVariantMap messageByKey(const QString &accountEmail, const QString &folder, const QString &uid) const;
    Q_INVOKABLE QVariantList messagesForThread(const QString &accountEmail, const QString &threadId) const;
    Q_INVOKABLE bool hasUsableBodyForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const;
    Q_INVOKABLE bool updateBodyForKey(const QString &accountEmail,
                                      const QString &folder,
                                      const QString &uid,
                                      const QString &bodyHtml);
    Q_INVOKABLE void notifyDataChanged();
    Q_INVOKABLE void reloadFolders();
    Q_INVOKABLE QVariantList messagesForSelection(const QString &folderKey,
                                                  const QStringList &selectedCategories,
                                                  int selectedCategoryIndex,
                                                  int limit = -1,
                                                  int offset = 0,
                                                  bool *hasMore = nullptr) const;
    Q_INVOKABLE QVariantList groupedMessagesForSelection(const QString &folderKey,
                                                         const QStringList &selectedCategories,
                                                         int selectedCategoryIndex,
                                                         bool todayExpanded,
                                                         bool yesterdayExpanded,
                                                         bool lastWeekExpanded,
                                                         bool twoWeeksAgoExpanded,
                                                         bool olderExpanded) const;
    Q_INVOKABLE QVariantMap statsForFolder(const QString &folderKey, const QString &rawFolderName) const;
    Q_INVOKABLE int newMessageCount(const QString &folderKey) const;
    Q_INVOKABLE void clearNewMessageCounts(const QString &folderKey);
    Q_INVOKABLE bool hasCachedHeadersForFolder(const QString &rawFolderName, int minCount = 60) const;
    Q_INVOKABLE QStringList inboxCategoryTabs() const;
    Q_INVOKABLE QVariantList tagItems() const;
    Q_INVOKABLE QString avatarForEmail(const QString &email) const;
    Q_INVOKABLE QString displayNameForEmail(const QString &email) const;
    Q_INVOKABLE QString preferredSelfDisplayName(const QString &accountEmail) const;
    Q_INVOKABLE QVariantMap migrationStats() const;
    Q_INVOKABLE bool avatarShouldRefresh(const QString &email, int ttlSeconds = 3600, int maxFailures = 3) const;
    Q_INVOKABLE QStringList staleGooglePeopleEmails(int limit = 20) const;
    Q_INVOKABLE void updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source);
    Q_INVOKABLE bool isSenderTrusted(const QString &domain) const;
    Q_INVOKABLE void setTrustedSenderDomain(const QString &domain);
    Q_INVOKABLE QVariantList attachmentsForMessage(const QString &accountEmail, const QString &folder, const QString &uid) const;
    Q_INVOKABLE QVariantList searchContacts(const QString &prefix, int limit = 10) const;
    Q_INVOKABLE QVariantList searchMessages(const QString &query, int limit = 50, int offset = 0, bool *hasMore = nullptr) const;
    Q_INVOKABLE QVariantList recentSearches(int limit = 5) const;
    Q_INVOKABLE void addRecentSearch(const QString &query);
    Q_INVOKABLE void removeRecentSearch(const QString &query);
    void upsertAttachments(qint64 messageId, const QString &accountEmail, const QVariantList &attachments);

    // Favorites sidebar config — which items are visible.
    Q_INVOKABLE QVariantList favoritesConfig() const;
    Q_INVOKABLE void setFavoriteEnabled(const QString &key, bool enabled);

    // User-created local folders.
    Q_INVOKABLE QVariantList userFolders() const;
    Q_INVOKABLE bool createUserFolder(const QString &name);
    Q_INVOKABLE bool deleteUserFolder(const QString &name);

signals:
    void dataChanged();
    void foldersChanged();
    void favoritesConfigChanged();
    void userFoldersChanged();
    // Emitted immediately after the DB is updated, before the full inbox reload.
    // Allows the message list to update just the unread dot for a single row instantly.
    void messageMarkedRead(const QString &accountEmail, const QString &uid);
    // Emitted after flagged state is updated for a single message.
    void messageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged);
    // Emitted after body_html is stored for a message. Does NOT trigger an inbox
    // reload — QML bindings that need the fresh body should depend on this signal.
    void bodyHtmlUpdated(const QString &accountEmail, const QString &folder, const QString &uid);

private:
    QString m_connectionName;
    QString m_dbPath;
    QVariantList m_folders;
    std::atomic<bool> m_reloadInboxScheduled{false};
    std::atomic<bool> m_pendingFoldersReload{false};

    // Cache for statsForFolder() results — pre-warmed on worker thread before foldersChanged is
    // emitted, so QML delegates never run DB queries on the UI thread.
    mutable QMutex m_folderStatsCacheMutex;
    mutable QHash<QString, QVariantMap> m_folderStatsCache;

    // Cache for tagItems() — pre-warmed alongside statsForFolder so tagFolderItems() in QML
    // returns instantly when foldersChanged fires (avoids correlated GROUP BY on UI thread).
    mutable QMutex m_tagItemsCacheMutex;
    mutable QVariantList m_tagItemsCache;
    mutable bool m_tagItemsCacheValid{false};

    // Per-thread SQLite connections — keyed by thread ID (quintptr).
    // Each worker thread gets its own connection, eliminating BlockingQueuedConnection
    // round-trips to the UI thread for every DataStore read.
    mutable QMutex m_connMutex;
    mutable QHash<quintptr, QString> m_threadConnections;

    // In-memory counters for new (unseen-by-user) messages per folder.
    // Keyed by lowercase raw folder name (e.g. "inbox", "[gmail]/categories/primary").
    mutable QMutex m_newCountMutex;
    QHash<QString, int> m_newMessageCounts;

    // In-memory cache for contact_avatars lookups — keyed by normalised email.
    // Populated on first DB hit; invalidated/updated on upsert. Eliminates
    // repeated SQL queries during list-scroll delegate creation.
    mutable QMutex m_avatarCacheMutex;
    mutable QHash<QString, QString> m_avatarCache;

    QSqlDatabase db() const;
    void scheduleDataChangedSignal();
    void incrementNewMessageCount(const QString &rawFolder);
    // Returns the set of keys to pass to statsForFolder() pre-warm, derived from m_folders.
    QStringList statsKeysFromFolders() const;
    // Pre-warms m_folderStatsCache on a worker thread, then invokes callback on the UI thread.
    void warmStatsCacheThen(std::function<void()> callback);
    QString avatarDirPath() const;
    // Parses a data URI, writes bytes to avatarDirPath(), stores file:// URL in DB.
    // Returns the file:// URL on success, empty string on failure.
    QString writeAvatarDataUri(const QString &email, const QString &dataUri);
};
