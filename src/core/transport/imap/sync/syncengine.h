#pragma once

#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

#include <atomic>
#include <functional>

#include "../connection/imapconnection.h"

namespace Imap {

struct SyncContext {
    std::shared_ptr<Connection> cxn;
    QString folderName;

    qint64 minUidExclusive = 0;
    bool reconcileDeletes = false;
    int fetchBudget = -1;

    std::atomic_bool *cancelRequested = nullptr;
    std::function<void(const QVariantMap&)> onHeader;

    std::function<bool(const QString &email, int ttlSecs, int maxFailures)> avatarShouldRefresh;
    std::function<QStringList(const QString &email, const QString &folder)> getFolderUids;
    std::function<void(const QString &email, const QStringList &uids)> removeUids;
    std::function<void(const QString &email, const QString &folder, const QStringList &readUids)> onFlagsReconciled;

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
    static QVariantList fetchFolders(const QString &host,
                                     qint32 port,
                                     const QString &email,
                                     const QString &accessToken,
                                     QString *statusOut);

    SyncResult execute(SyncContext &ctx);
};

} // namespace Imap
