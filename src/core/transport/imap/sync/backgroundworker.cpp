#include "backgroundworker.h"

#include "syncutils.h"
#include "syncengine.h"
#include "../imapservice.h"

#include <QSet>
#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;

namespace Imap {

std::pair<bool, QString>
BackgroundWorker::resolveAccount() {
    static int consecutiveFailures = 0;

    QVariantList accounts;
    emit requestAccounts(&accounts);

    const auto [accountOk, accountErr, target] = SyncUtils::selectOAuthAccount(accounts);
    if (!accountOk) {
        SyncUtils::handleFailure([this](const bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                 m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                 consecutiveFailures, accountErr, 10, &m_running);
        return {false, accountErr};
    }

    QString accessToken;
    emit requestRefreshAccessToken(target.account, target.email, &accessToken);

    if (accessToken.isEmpty()) {
        constexpr auto err = "Realtime sync: auth refresh failed; retrying."_L1;
        SyncUtils::handleFailure([this](const bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                 m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                 consecutiveFailures, err, 15, &m_running);
        return {false, err};
    }

    m_activeAccount = target.account;
    m_activeEmail = target.email;
    m_activeAccessToken = accessToken;

    consecutiveFailures = 0;
    return {true, {}};
}

BackgroundWorker::FolderStatus
BackgroundWorker::fetchFolderStatus(const QString &folder) const {
    FolderStatus out;

    auto cxn = ImapService::getPooledConnection(m_activeEmail);
    if (!cxn)
        return out;

    QString mailbox = folder;
    mailbox.replace("\\"_L1, "\\\\"_L1);
    mailbox.replace("\""_L1, "\\\""_L1);

    const auto resp = cxn->execute(
        QStringLiteral("STATUS \"%1\" (UIDNEXT HIGHESTMODSEQ MESSAGES)").arg(mailbox));

    static const QRegularExpression uidNextRe(QStringLiteral("\\bUIDNEXT\\s+(\\d+)"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression modSeqRe(QStringLiteral("\\bHIGHESTMODSEQ\\s+(\\d+)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression messagesRe(QStringLiteral("\\bMESSAGES\\s+(\\d+)"),
                                                QRegularExpression::CaseInsensitiveOption);

    if (const auto m = uidNextRe.match(resp); m.hasMatch())
        out.uidNext = m.captured(1).toLongLong();
    if (const auto m = modSeqRe.match(resp); m.hasMatch())
        out.highestModSeq = m.captured(1).toLongLong();
    if (const auto m = messagesRe.match(resp); m.hasMatch())
        out.messages = m.captured(1).toLongLong();

    return out;
}

void
BackgroundWorker::doBootstrap() {
    m_bootstrapped = true;
}

void
BackgroundWorker::start() {
    if (m_running.exchange(true))
        return;

    while (m_running) {
        if (const auto [ok, err] = resolveAccount(); !ok) {
            emit loopError(err);
            continue;
        }

        if (!m_bootstrapped.load())
            doBootstrap();

        auto pooled = ImapService::getPooledConnection(m_activeEmail);
        if (!pooled) {
            emit loopError("Background sync: pooled connection unavailable."_L1);
            SyncUtils::sleepInterruptible(m_running, 2);
            continue;
        }

        QString folderStatus;
        const QVariantList folderRows = SyncEngine::fetchFolders(pooled, &folderStatus, !m_bootstrapped.load());
        emit upsertFoldersRequested(folderRows);

        QSet<QString> parentContainers;
        for (const QVariant &f : folderRows) {
            const QString name = f.toMap().value("name"_L1).toString().trimmed();
            const int slash = name.indexOf('/');
            if (slash > 0)
                parentContainers.insert(name.left(slash).toLower());
        }

        QStringList folders;
        folders.reserve(folderRows.size());
        for (const QVariant &f : folderRows) {
            const auto row = f.toMap();
            const QString name = row.value("name"_L1).toString().trimmed();
            const QString flags = row.value("flags"_L1).toString().toLower();
            const QString lowerName = name.toLower();

            const bool isNoSelect = flags.contains("\\noselect"_L1);
            const bool isCategory = name.contains("/Categories/"_L1, Qt::CaseInsensitive);
            const bool isContainerRoot = name.compare("[Gmail]"_L1, Qt::CaseInsensitive) == 0
                                      || name.compare("[Google Mail]"_L1, Qt::CaseInsensitive) == 0;
            const bool isParentContainer = parentContainers.contains(lowerName);

            if (name.isEmpty() || isNoSelect || isCategory || isContainerRoot || isParentContainer)
                continue;
            folders.push_back(name);
        }

        for (const auto &folder : folders) {
            if (!m_running.load())
                break;

            const auto status = fetchFolderStatus(folder);
            const auto key = folder.trimmed().toLower();
            const bool firstStatusSeen = !m_lastFolderStatus.contains(key);

            FolderStatus previousStatus = m_lastFolderStatus.value(key);
            if (firstStatusSeen) {
                bool found = false;
                qint64 uidNext = -1;
                qint64 highestModSeq = -1;
                qint64 messages = -1;
                emit loadFolderStatusSnapshotRequested(m_activeEmail, folder, &uidNext, &highestModSeq, &messages, &found);
                if (found) {
                    previousStatus.uidNext = uidNext;
                    previousStatus.highestModSeq = highestModSeq;
                    previousStatus.messages = messages;
                }
            }
            m_lastFolderStatus.insert(key, status);
            emit saveFolderStatusSnapshotRequested(m_activeEmail, folder,
                                                   status.uidNext, status.highestModSeq, status.messages);

            const auto statusAvailable = (status.uidNext > 0 || status.highestModSeq > 0 || status.messages >= 0);
            const auto changedByStatus = !statusAvailable || firstStatusSeen
                                      || status.uidNext != previousStatus.uidNext
                                      || (status.highestModSeq > 0 && status.highestModSeq != previousStatus.highestModSeq)
                                      || (status.messages >= 0 && status.messages != previousStatus.messages);

            if (!changedByStatus)
                continue;

            emit syncHeadersAndFlagsRequested(m_activeAccount, m_activeEmail, folder, m_activeAccessToken);
            emit fetchBodiesRequested(m_activeAccount, m_activeEmail, folder, m_activeAccessToken);
        }

        emit idleLiveUpdateRequested(m_activeAccount, m_activeEmail);

        if (m_realtimeDegradedNotified.load()) {
            m_realtimeDegradedNotified = false;
            SyncUtils::maybeEmitRealtime([this](const bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                         m_lastRealtimeStatusMs, true, "Realtime sync recovered."_L1, 0);
        }

        SyncUtils::sleepInterruptible(m_running, m_intervalSeconds);
    }

    m_running = false;
}

} // namespace Imap
