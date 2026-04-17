#pragma once

#include "contactstore.h"
#include "folderstatsstore.h"

#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

/// Callbacks that MessageStore uses to communicate with the DataStore facade
/// (signal dispatch, notification gating, etc.).
struct MessageStoreCallbacks {
    std::function<void()> scheduleDataChanged;
    std::function<bool()> desktopNotifyEnabled;
    /// Dispatched to UI thread via QueuedConnection by DataStore.
    std::function<void(const QVariantMap &)> onNewMail;
};

/// Owns all message CRUD, query, search, and attachment operations.
/// Thread-safe: uses the same per-thread SQLite connection pool as DataStore.
class MessageStore
{
public:
    using DbAccessor = std::function<QSqlDatabase()>;

    explicit MessageStore(DbAccessor dbAccessor,
                          ContactStore &contacts,
                          FolderStatsStore &folderStats,
                          MessageStoreCallbacks callbacks);

    // ── Upsert / ingest ─────────────────────────────────────────────
    void upsertHeaders(const QVariantList &headers) const;
    void upsertHeader(const QVariantMap &header) const;

    // ── Prune / remove ──────────────────────────────────────────────
    void pruneFolderToUids(const QString &accountEmail, const QString &folder,
                           const QStringList &uids) const;
    void removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids,
                                     bool skipOrphanCleanup = false) const;

    // ── Flag reconciliation ─────────────────────────────────────────
    void markMessageRead(const QString &accountEmail, const QString &uid) const;
    void reconcileReadFlags(const QString &accountEmail, const QString &folder,
                            const QStringList &readUids) const;
    void markMessageFlagged(const QString &accountEmail, const QString &uid, bool flagged) const;
    void reconcileFlaggedUids(const QString &accountEmail, const QString &folder,
                              const QStringList &flaggedUids) const;

    // ── Edge CRUD ───────────────────────────────────────────────────
    QVariantMap folderMapRowForEdge(const QString &accountEmail, const QString &folder,
                                   const QString &uid) const;
    void deleteSingleFolderEdge(const QString &accountEmail, const QString &folder,
                                const QString &uid) const;
    void deleteFolderEdges(const QString &accountEmail, const QString &folder,
                           const QStringList &uids) const;
    void deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder,
                                     qint64 messageId) const;
    void upsertLabel(const QString &accountEmail, qint64 messageId, const QString &label) const;
    QString folderUidForMessageId(const QString &accountEmail, const QString &folder,
                                  qint64 messageId) const;
    void insertFolderEdge(const QString &accountEmail, qint64 messageId,
                          const QString &folder, const QString &uid, qint32 unread,
                          const QString &source = QStringLiteral("imap-label")) const;
    QMap<QString, qint64> lookupByMessageIdHeaders(const QString &accountEmail,
                                                   const QStringList &messageIdHeaders) const;
    void removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId) const;

    // ── Folder UID queries ──────────────────────────────────────────
    QStringList folderUids(const QString &accountEmail, const QString &folder) const;
    QStringList folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const;
    qint64 folderMaxUid(const QString &accountEmail, const QString &folder) const;
    qint64 folderMessageCount(const QString &accountEmail, const QString &folder) const;

    // ── Body fetch candidates ───────────────────────────────────────
    QStringList bodyFetchCandidates(const QString &accountEmail, const QString &folder,
                                   qint32 limit = 10) const;
    QVariantList bodyFetchCandidatesByAccount(const QString &accountEmail,
                                             qint32 limit = 10) const;
    QVariantList fetchCandidatesForMessageKey(const QString &accountEmail,
                                             const QString &folder,
                                             const QString &uid) const;

    // ── Message queries ─────────────────────────────────────────────
    bool hasUsableBodyForEdge(const QString &accountEmail, const QString &folder,
                             const QString &uid) const;
    QVariantMap messageByKey(const QString &accountEmail, const QString &folder,
                             const QString &uid) const;
    QVariantList messagesForThread(const QString &accountEmail, const QString &threadId) const;
    bool updateBodyForKey(const QString &accountEmail, const QString &folder,
                          const QString &uid, const QString &bodyHtml) const;

    // ── Selection / list queries ────────────────────────────────────
    QVariantList messagesForSelection(const QString &folderKey,
                                     const QStringList &selectedCategories,
                                     qint32 selectedCategoryIndex,
                                     qint32 limit = -1,
                                     qint32 offset = 0,
                                     bool *hasMore = nullptr) const;
    QVariantList groupedMessagesForSelection(const QString &folderKey,
                                            const QStringList &selectedCategories,
                                            qint32 selectedCategoryIndex,
                                            bool todayExpanded,
                                            bool yesterdayExpanded,
                                            bool lastWeekExpanded,
                                            bool twoWeeksAgoExpanded,
                                            bool olderExpanded) const;

    // ── Attachments ─────────────────────────────────────────────────
    void upsertAttachments(qint64 messageId, const QString &accountEmail,
                           const QVariantList &attachments) const;
    QVariantList attachmentsForMessage(const QString &accountEmail, const QString &folder,
                                      const QString &uid) const;
    void setAttachmentLocalPath(const QString &accountEmail, qint64 messageId,
                                const QString &partId, const QString &localPath) const;

    // ── Search ──────────────────────────────────────────────────────
    QVariantList searchMessages(const QString &query, qint32 limit = 50,
                                qint32 offset = 0, bool *hasMore = nullptr) const;

    // ── Threading helpers (shared with DataStore::init backfill) ──
    static QString computeThreadId(const QString &refs, const QString &irt, const QString &ownMsgId);

private:
    DbAccessor m_db;
    ContactStore &m_contacts;
    FolderStatsStore &m_folderStats;
    MessageStoreCallbacks m_callbacks;
};
