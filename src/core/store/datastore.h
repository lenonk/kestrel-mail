#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>

class QSqlDatabase;

class DataStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList inbox READ inbox NOTIFY inboxChanged)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QStringList inboxCategoryTabs READ inboxCategoryTabs NOTIFY inboxChanged)
public:
    explicit DataStore(QObject *parent = nullptr);
    ~DataStore() override;

    QVariantList inbox() const;
    QVariantList folders() const;

    Q_INVOKABLE bool init();
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
    // Returns {messageId (qint64), unread (int)} for a specific folder/uid edge.
    QVariantMap folderMapRowForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const;
    // Deletes the specific (folder, uid) edge, cleans orphaned messages, reloads inbox.
    void deleteSingleFolderEdge(const QString &accountEmail, const QString &folder, const QString &uid);
    // Inserts a new (folder, uid) edge for an already-known message_id (used after UID MOVE COPYUID).
    void insertFolderEdge(const QString &accountEmail, qint64 messageId, const QString &folder, const QString &uid, int unread);
    // Removes ALL folder edges for a message_id (fallback when server gives no COPYUID).
    void removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId);
    Q_INVOKABLE QStringList folderUids(const QString &accountEmail, const QString &folder) const;
    QStringList folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMaxUid(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE qint64 folderMessageCount(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE QVariantMap folderSyncStatus(const QString &accountEmail, const QString &folder) const;
    Q_INVOKABLE void upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                            qint64 uidNext, qint64 highestModSeq, qint64 messages);
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
    Q_INVOKABLE void reloadInbox();
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
    void upsertAttachments(qint64 messageId, const QString &accountEmail, const QVariantList &attachments);

signals:
    void inboxChanged();
    void foldersChanged();
    // Emitted immediately after the DB is updated, before the full inbox reload.
    // Allows the message list to update just the unread dot for a single row instantly.
    void messageMarkedRead(const QString &accountEmail, const QString &uid);
    // Emitted after body_html is stored for a message. Does NOT trigger an inbox
    // reload — QML bindings that need the fresh body should depend on this signal.
    void bodyHtmlUpdated(const QString &accountEmail, const QString &folder, const QString &uid);

private:
    QString m_connectionName;
    QVariantList m_inbox;
    QVariantList m_folders;
    bool m_reloadInboxScheduled = false;

    QSqlDatabase db() const;
    void scheduleReloadInbox();
};
