#pragma once

#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <functional>
#include <atomic>
#include "../connection/imapconnection.h"

namespace Imap {

/**
 * Configuration and context for a folder sync operation.
 * Encapsulates all inputs needed by sync strategies.
 *
 * The Connection object (ctx.cxn) must already be authenticated via
 * connectAndAuth() before execute() is called. SyncEngine::execute()
 * handles SELECT internally.
 */
struct SyncContext {
    // Connection (must be non-null, authenticated via connectAndAuth)
    std::shared_ptr<Connection> cxn;

    // Folder to sync
    QString folderName;

    // Sync mode
    qint64 minUidExclusive = 0;  // >0 = incremental, 0 = full
    bool reconcileDeletes = false;

    // Budget: -1 = unlimited, 0 = incremental only (no backfill), >0 = cap at N messages
    int fetchBudget = -1;

    // Cancellation (optional; checked periodically during fetch)
    std::atomic_bool *cancelRequested = nullptr;

    // Per-message callback (called for each header as it is ingested)
    std::function<void(const QVariantMap&)> onHeader;

    // DataStore operations — injected to avoid coupling to DataStore type.
    // All may be null; callers should check before invoking.

    // Returns true if an avatar lookup for `email` is allowed given TTL/failure policy.
    std::function<bool(const QString &email, int ttlSecs, int maxFailures)> avatarShouldRefresh;

    // Returns UIDs already stored locally for a given account+folder.
    // Called on whichever thread SyncEngine runs on via BlockingQueuedConnection.
    std::function<QStringList(const QString &email, const QString &folder)> getFolderUids;

    // Remove UIDs from every folder for the given account (delete reconciliation).
    std::function<void(const QString &email, const QStringList &uids)> removeUids;

    // Batch read-flag reconciliation: called with UIDs server confirms are read.
    // Only marks messages read (never unread).
    std::function<void(const QString &email, const QString &folder, const QStringList &readUids)> onFlagsReconciled;

    // Convenience helpers
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

/**
 * Result of a sync operation.
 */
struct SyncResult {
    bool success = false;
    QString statusMessage;
    QVariantList headers;
    int fetchedCount = 0;
    int categoryMissCount = 0;
    int missingAddressHeadersCount = 0;
};

} // namespace Imap
