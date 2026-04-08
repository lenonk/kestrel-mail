#pragma once

#include "contactstore.h"
#include "folderstatsstore.h"
#include "messagestore.h"
#include "userprefsstore.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

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

    // ── Message CRUD forwarding — delegates to m_messages ───────────
    Q_INVOKABLE void upsertHeader(const QVariantMap &header);
    Q_INVOKABLE void upsertHeaders(const QVariantList &headers);
    Q_INVOKABLE void pruneFolderToUids(const QString &accountEmail, const QString &folder, const QStringList &uids);
    Q_INVOKABLE void removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids,
                                                   bool skipOrphanCleanup = false);
    void reconcileReadFlags(const QString &accountEmail, const QString &folder,
                            const QStringList &readUids);
    Q_INVOKABLE void markMessageRead(const QString &accountEmail, const QString &uid);
    void markMessageFlagged(const QString &accountEmail, const QString &uid, bool flagged);
    void reconcileFlaggedUids(const QString &accountEmail, const QString &folder,
                              const QStringList &flaggedUids);
    QVariantMap folderMapRowForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const;
    void deleteSingleFolderEdge(const QString &accountEmail, const QString &folder, const QString &uid);
    void deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder, qint64 messageId);
    QString folderUidForMessageId(const QString &accountEmail, const QString &folder, qint64 messageId) const;
    void insertFolderEdge(const QString &accountEmail, qint64 messageId, const QString &folder, const QString &uid, int unread, const QString &source = QStringLiteral("imap-label"));
    QMap<QString, qint64> lookupByMessageIdHeaders(const QString &accountEmail, const QStringList &messageIdHeaders);
    void removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId);
    Q_INVOKABLE QStringList folderUids(const QString &accountEmail, const QString &folder) const;
    QStringList folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMaxUid(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMessageCount(const QString &accountEmail, const QString &folder) const;
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
    Q_INVOKABLE QVariantList attachmentsForMessage(const QString &accountEmail, const QString &folder, const QString &uid) const;
    Q_INVOKABLE QVariantList searchMessages(const QString &query, int limit = 50, int offset = 0, bool *hasMore = nullptr) const;
    void upsertAttachments(qint64 messageId, const QString &accountEmail, const QVariantList &attachments);
    void setAttachmentLocalPath(const QString &accountEmail, qint64 messageId, const QString &partId, const QString &localPath);

    // ── Folder management ───────────────────────────────────────────
    Q_INVOKABLE void upsertFolder(const QVariantMap &folder);
    Q_INVOKABLE void notifyDataChanged();
    Q_INVOKABLE void reloadFolders();
    Q_INVOKABLE void clearNewMessageCounts(const QString &folderKey);

    // ── Folder stats forwarding — delegates to m_folderStats ────────
    Q_INVOKABLE QVariantMap folderSyncStatus(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE void upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                            qint64 uidNext, qint64 highestModSeq, qint64 messages);
    Q_INVOKABLE qint64 folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE void updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder,
                                                qint64 modseq);
    Q_INVOKABLE QVariantMap statsForFolder(const QString &folderKey, const QString &rawFolderName) const;
    Q_INVOKABLE int newMessageCount(const QString &folderKey) const;
    Q_INVOKABLE bool hasCachedHeadersForFolder(const QString &rawFolderName, int minCount = 60) const;
    Q_INVOKABLE QStringList inboxCategoryTabs() const;
    Q_INVOKABLE QVariantList tagItems() const;

    // ── Contact / avatar forwarding — delegates to m_contacts ───────
    Q_INVOKABLE QString avatarForEmail(const QString &email) const;
    Q_INVOKABLE QString displayNameForEmail(const QString &email) const;
    Q_INVOKABLE QString preferredSelfDisplayName(const QString &accountEmail) const;
    Q_INVOKABLE bool avatarShouldRefresh(const QString &email, int ttlSeconds = 3600, int maxFailures = 3) const;
    Q_INVOKABLE QStringList staleGooglePeopleEmails(int limit = 20) const;
    Q_INVOKABLE void updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source);
    Q_INVOKABLE QVariantList searchContacts(const QString &prefix, int limit = 10) const;

    // ── User prefs forwarding — delegates to m_prefs ────────────────
    Q_INVOKABLE QVariantMap migrationStats() const;
    Q_INVOKABLE bool isSenderTrusted(const QString &domain) const;
    Q_INVOKABLE void setTrustedSenderDomain(const QString &domain);
    Q_INVOKABLE QVariantList favoritesConfig() const;
    Q_INVOKABLE void setFavoriteEnabled(const QString &key, bool enabled);
    Q_INVOKABLE QVariantList userFolders() const;
    Q_INVOKABLE bool createUserFolder(const QString &name);
    Q_INVOKABLE bool deleteUserFolder(const QString &name);
    Q_INVOKABLE QVariantList recentSearches(int limit = 5) const;
    Q_INVOKABLE void addRecentSearch(const QString &query);
    Q_INVOKABLE void removeRecentSearch(const QString &query);

signals:
    void dataChanged();
    void foldersChanged();
    void favoritesConfigChanged();
    void userFoldersChanged();
    void messageMarkedRead(const QString &accountEmail, const QString &uid);
    void messageFlaggedChanged(const QString &accountEmail, const QString &uid, bool flagged);
    void bodyHtmlUpdated(const QString &accountEmail, const QString &folder, const QString &uid);
    void newMailReceived(const QVariantMap &info);
    void notificationReplyRequested(const QString &accountEmail, const QString &folder, const QString &uid);

public:
    bool desktopNotifyEnabled() const { return m_desktopNotifyEnabled.load(); }
    void setDesktopNotifyEnabled(bool enabled) { m_desktopNotifyEnabled.store(enabled); }
    int inboxCount() const;

    // Static avatar helpers — forward to ContactStore.
    Q_INVOKABLE static QString avatarInitials(const QString &displayName, const QString &fallback)
    { return ContactStore::avatarInitials(displayName, fallback); }
    Q_INVOKABLE static QColor avatarColor(const QString &displayName, const QString &fallback)
    { return ContactStore::avatarColor(displayName, fallback); }
    static QPixmap avatarPixmap(const QString &displayName, const QString &email, int size = 64)
    { return ContactStore::avatarPixmap(displayName, email, size); }

    /// Direct access to the ContactStore (for callers that need it without the facade).
    ContactStore &contacts() { return *m_contacts; }

private:
    QString m_connectionName;
    QString m_dbPath;
    std::atomic<bool> m_reloadInboxScheduled{false};
    std::atomic<bool> m_pendingFoldersReload{false};
    std::atomic<bool> m_desktopNotifyEnabled{false};

    mutable QMutex m_connMutex;
    mutable QHash<quintptr, QString> m_threadConnections;

    std::unique_ptr<ContactStore> m_contacts;
    std::unique_ptr<UserPrefsStore> m_prefs;
    std::unique_ptr<FolderStatsStore> m_folderStats;
    std::unique_ptr<MessageStore> m_messages;

    QSqlDatabase db() const;
    void scheduleDataChangedSignal();
    void warmStatsCacheThen(std::function<void()> callback);
};
