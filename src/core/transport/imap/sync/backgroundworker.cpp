#include "backgroundworker.h"

#include "syncutils.h"
#include "syncengine.h"
#include "../connection/connectionpool.h"

#include <QSet>
#include <QRegularExpression>
#include <QtConcurrentRun>

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
        // Don't fail the worker loop solely on refresh miss; pooled connections may
        // still be valid, and we may have a previously usable token.
        if (!m_activeAccessToken.isEmpty() && m_activeEmail.compare(target.email, Qt::CaseInsensitive) == 0)
            accessToken = m_activeAccessToken;
    }

    m_activeAccount = target.account;
    m_activeEmail = target.email;
    m_activeAccessToken = accessToken;

    consecutiveFailures = 0;
    return {true, {}};
}

QHash<QString, BackgroundWorker::FolderStatus>
BackgroundWorker::fetchAllFolderStatuses() const {
    QHash<QString, FolderStatus> out;

    std::shared_ptr<Connection> cxn;
    while (m_running.load() && !cxn)
        cxn = m_pool ? m_pool->acquire("bg-folder-status"_L1, m_activeEmail) : nullptr;
    if (!cxn)
        return out;

    static const QRegularExpression uidNextRe("\\bUIDNEXT\\s+(\\d+)"_L1,
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression modSeqRe("\\bHIGHESTMODSEQ\\s+(\\d+)"_L1,
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression messagesRe("\\bMESSAGES\\s+(\\d+)"_L1,
                                                QRegularExpression::CaseInsensitiveOption);

    // Prefer LIST-STATUS (RFC 5819) — fetches all folder statuses in one round trip.
    // Fall back to per-folder STATUS when the server doesn't advertise LIST-STATUS.
    if (cxn->capabilities().contains("LIST-STATUS"_L1, Qt::CaseInsensitive)) {
        const QString resp = cxn->execute(
            R"(LIST "" "*" RETURN (STATUS (MESSAGES UIDNEXT HIGHESTMODSEQ)))"_L1);

        // Server sends interleaved * LIST and * STATUS untagged responses.
        // Parse each * STATUS line to extract per-folder values.
        static const QRegularExpression statusLineRe(
            R"~(\* STATUS "([^"]+)"\s*\(([^)]+)\))~"_L1,
            QRegularExpression::CaseInsensitiveOption);

        auto it = statusLineRe.globalMatch(resp);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString folderKey = m.captured(1).trimmed().toLower();
            const QString statusData = m.captured(2);

            FolderStatus fs;
            if (const auto m2 = uidNextRe.match(statusData);  m2.hasMatch())
                fs.uidNext      = m2.captured(1).toLongLong();
            if (const auto m2 = modSeqRe.match(statusData);   m2.hasMatch())
                fs.highestModSeq = m2.captured(1).toLongLong();
            if (const auto m2 = messagesRe.match(statusData);  m2.hasMatch())
                fs.messages     = m2.captured(1).toLongLong();
            out.insert(folderKey, fs);
        }
    } else {
        // Fallback: per-folder STATUS (one round trip per folder).
        // Collect all known folder names from a fresh LIST.
        const QString listResp = cxn->execute(R"(LIST "" "*")"_L1);
        static const QRegularExpression folderNameRe(
            R"~(\* LIST[^\r\n]+"([^"]+)"\s*$)~"_L1,
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

        auto fit = folderNameRe.globalMatch(listResp);
        while (fit.hasNext() && m_running.load()) {
            const QString folder = fit.next().captured(1).trimmed();
            if (folder.isEmpty())
                continue;

            QString mailbox = folder;
            mailbox.replace("\\"_L1, "\\\\"_L1);
            mailbox.replace("\""_L1, "\\\""_L1);

            const QString resp = cxn->execute(
                "STATUS \"%1\" (UIDNEXT HIGHESTMODSEQ MESSAGES)"_L1.arg(mailbox));

            FolderStatus fs;
            if (const auto m = uidNextRe.match(resp);   m.hasMatch())
                fs.uidNext       = m.captured(1).toLongLong();
            if (const auto m = modSeqRe.match(resp);    m.hasMatch())
                fs.highestModSeq = m.captured(1).toLongLong();
            if (const auto m = messagesRe.match(resp);  m.hasMatch())
                fs.messages      = m.captured(1).toLongLong();
            out.insert(folder.trimmed().toLower(), fs);
        }
    }

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

        std::shared_ptr<Connection> pooled;
        while (m_running.load() && !pooled)
            pooled = m_pool ? m_pool->acquire("bg-list-folders"_L1, m_activeEmail) : nullptr;
        if (!pooled)
            continue;

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

        // Fetch all folder statuses in one LIST-STATUS round trip (or per-folder fallback).
        const auto allStatuses = fetchAllFolderStatuses();

        for (const auto &folder : folders) {
            if (!m_running.load())
                break;

            const auto key = folder.trimmed().toLower();
            const auto status = allStatuses.value(key);
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

            const auto account = m_activeAccount;
            const auto email = m_activeEmail;
            const auto token = m_activeAccessToken;
            const auto folderCopy = folder;
            const auto dispatched = QtConcurrent::run([this, account, email, token, folderCopy]() {
                emit syncHeadersAndFlagsRequested(account, email, folderCopy, token);
            });
            Q_UNUSED(dispatched);
        }

        emit idleLiveUpdateRequested(m_activeAccount, m_activeEmail);

        if (m_realtimeDegradedNotified.load()) {
            m_realtimeDegradedNotified = false;
            SyncUtils::maybeEmitRealtime([this](const bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                         m_lastRealtimeStatusMs, true, "Realtime sync recovered."_L1, 0);
        }

        bool throttled = false;
        emit requestAccountThrottled(m_activeEmail, &throttled);

        const int sleepSeconds = throttled ? 300 : m_intervalSeconds;
        if (throttled)
            qInfo().noquote() << "[bg-worker] throttled -> extending sleep to" << sleepSeconds << "seconds for" << m_activeEmail;

        SyncUtils::sleepInterruptible(m_running, sleepSeconds);
    }

    m_running = false;
}

} // namespace Imap
