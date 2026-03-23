#pragma once

#include <QMap>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#include "../connection/imapconnection.h"

namespace Imap {

struct SyncContext {
    std::shared_ptr<Connection> cxn;
    QString folderName;

    qint64 minUidExclusive = 0;
    qint64 remoteExists = -1;
    bool hasSearchAllSnapshot = false;
    QStringList searchAllUids;
    bool reconcileDeletes = false;
    int fetchBudget = -1;

    // CONDSTORE: modseq stored at the end of the previous sync (0 = unknown / first sync).
    // When the EXAMINE response returns the same modseq, nothing has changed and expensive
    // UID SEARCH ALL operations can be skipped.
    qint64 lastHighestModSeq = 0;
    // Set by SyncEngine::execute() after computing the CONDSTORE skip decision.
    // True when examineModSeq > 0 and matches lastHighestModSeq (no server-side changes).
    bool condstoreUnchanged = false;

    std::atomic_bool *cancelRequested = nullptr;
    std::function<void(const QVariantMap&)> onHeader;
    // Called after a successful sync with the HIGHESTMODSEQ from EXAMINE so the caller
    // can persist it for the next cycle's CONDSTORE check.
    std::function<void(qint64 highestModSeq)> onSyncStateUpdated;

    std::function<bool(const QString &email, int ttlSecs, int maxFailures)> avatarShouldRefresh;
    std::function<QStringList(const QString &email, const QString &folder)> getFolderUids;
    // Optional: exact raw message-edge count for folder (no thread collapsing).
    std::function<qint64(const QString &email, const QString &folder)> getFolderMessageCount;
    // Optional: returns UIDs of locally-known messages that need snippet regeneration.
    // executeFull will include these in its fetch pass even if they are already in getFolderUids.
    std::function<QStringList(const QString &email, const QString &folder)> getUidsNeedingSnippetRefresh;
    // Folder-scoped prune: removes folder edges not in remoteUids, then orphan-cleans.
    // Preferred over removeUids (all-folder) to avoid removing valid edges in other folders.
    std::function<void(const QString &email, const QString &folder, const QStringList &remoteUids)> pruneFolder;
    std::function<void(const QString &email, const QString &folder, const QStringList &readUids)> onFlagsReconciled;
    std::function<void(const QString &email, const QString &folder, const QStringList &flaggedUids)> onFlaggedReconciled;

    // Cross-folder dedup: look up which Message-ID headers are already in the DB.
    // Returns {message_id_header → messages.id} for known messages.
    std::function<QMap<QString,qint64>(const QString &accountEmail, const QStringList &messageIdHeaders)> lookupByMessageIdHeaders;
    // Insert a folder edge for an already-known message (cross-folder dedup shortcut).
    std::function<void(const QString &accountEmail, qint64 messageId, const QString &folder, const QString &uid, int unread)> insertFolderEdge;

    [[nodiscard]] bool isGmail() const {
        return cxn && cxn->host().contains(QStringLiteral("gmail"), Qt::CaseInsensitive);
    }
    [[nodiscard]] bool isInbox() const {
        return folderName.compare(QStringLiteral("INBOX"), Qt::CaseInsensitive) == 0;
    }
    [[nodiscard]] bool isGmailInbox() const {
        return isGmail() && isInbox();
    }
};

struct SyncResult {
    bool success = false;
    QString statusMessage;
    QVariantList headers;
    int fetchedCount = 0;
    int categoryMissCount = 0;
    int missingAddressHeadersCount = 0;
};

class SyncEngine {
public:
    static QVariantList fetchFolders(std::shared_ptr<Connection> cxn, QString *statusOut, bool refresh = false);

    SyncResult execute(SyncContext &ctx);
};

} // namespace Imap
