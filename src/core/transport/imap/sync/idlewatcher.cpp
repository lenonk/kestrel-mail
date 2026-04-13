#include "idlewatcher.h"

#include "syncutils.h"
#include "../connection/imapconnection.h"
#include "../parser/responseparser.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QThread>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;
using Imap::Parser::parseUidSearchAll;

namespace Imap {
namespace {

struct IdleSignals {
    bool mailboxChanged = false;
    qint32 existsSignals = 0;
    qint32 recentSignals = 0;
    qint32 expungeSignals = 0;
};

struct ReconcileResult {
    qint32 newCount = 0;
    qsizetype deletedCount = 0;
    qint64 maxLocalUidSeen = 0;
};

[[nodiscard]] IdleSignals
waitForIdleSignals(Connection &cxn, const std::atomic_bool &running) {
    IdleSignals out;
    QElapsedTimer idleTimer;

    idleTimer.start();

    while (running) {
        if (const auto push = cxn.waitForIdlePush(1000); !push.isEmpty()) {
            const auto hasExists  = push.contains(" EXISTS"_L1, Qt::CaseInsensitive);
            const auto hasRecent  = push.contains(" RECENT"_L1, Qt::CaseInsensitive);
            const auto hasExpunge = push.contains(" EXPUNGE"_L1, Qt::CaseInsensitive);

            if (hasExists || hasRecent || hasExpunge) {
                out.mailboxChanged = true;
                if (hasExists)  ++out.existsSignals;
                if (hasRecent)  ++out.recentSignals;
                if (hasExpunge) ++out.expungeSignals;
            }
        }

        if (out.mailboxChanged || idleTimer.elapsed() > 5 * 60 * 1000)
            break;
    }

    return out;
}

[[nodiscard]] qint64
maxUidFromSet(const QSet<QString> &uids) {
    qint64 maxUid = 0;

    for (const auto &u : uids) {
        bool ok = false;
        if (const auto v = u.toLongLong(&ok); ok && v > maxUid)
            maxUid = v;
    }

    return maxUid;
}

[[nodiscard]] int
countUidsAbove(const QSet<QString> &uids, const qint64 floorExclusive) {
    int count = 0;

    for (const auto &u : uids) {
        bool ok = false;
        if (const auto v = u.toLongLong(&ok); ok && v > floorExclusive)
            ++count;
    }
    return count;
}

[[nodiscard]] ReconcileResult
reconcileInbox(IdleWatcher *self, const QString &email, const QStringList &remoteInboxUids,
               std::atomic<qint64> &maxUidWatermark) {
    QStringList localInboxUids;
    emit self->requestFolderUids(email, "INBOX"_L1, &localInboxUids);

    const QSet remoteSet(remoteInboxUids.begin(), remoteInboxUids.end());
    const QSet localSet(localInboxUids.begin(), localInboxUids.end());

    const auto priorWatermark = maxUidWatermark.load();
    const auto maxLocalUid = maxUidFromSet(localSet);
    const auto maxLocalUidSeen = std::max(maxLocalUid, priorWatermark);

    if (maxLocalUidSeen > priorWatermark)
        maxUidWatermark.store(maxLocalUidSeen);

    const auto newCount = countUidsAbove(remoteSet, maxLocalUid);

    const auto deletedList = (localSet - remoteSet).values();
    const auto deletedCount = deletedList.size();

    emit self->pruneFolderToUidsRequested(email, "INBOX"_L1, remoteInboxUids);

    if (!deletedList.isEmpty())
        emit self->removeUidsRequested(email, deletedList);

    return {newCount, deletedCount, maxLocalUidSeen};
}

} // namespace

void
IdleWatcher::start() {
    if (m_running.exchange(true))
        return;

    qint32 consecutiveFailures = 0;
    QString lastAccessToken;
    QString lastEmail;

    // Persistent connection reused across IDLE cycles — avoids a full
    // TCP+TLS handshake + AUTHENTICATE on every 5-minute timeout.
    std::shared_ptr<Connection> cxn;
    bool inboxSelected = false;

    while (m_running) {
        QVariantList accounts;
        emit requestAccounts(&accounts);

        // If auth is suspended (refresh token revoked), sleep long and skip.
        if (authSuspended.load()) {
            cxn.reset();
            inboxSelected = false;
            SyncUtils::sleepInterruptible(m_running, 60);
            continue;
        }

        const auto [accountOk, accountErr, target] = SyncUtils::selectOAuthAccount(accounts);
        if (!accountOk) {
            cxn.reset();
            inboxSelected = false;
            SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                     m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                     consecutiveFailures, accountErr, 10);
            continue;
        }

        const bool accountChanged = lastEmail.compare(target.email, Qt::CaseInsensitive) != 0;

        // Reconnect only when there is no live connection or the account switched.
        if (!cxn || !cxn->isConnected() || accountChanged) {
            cxn.reset();
            inboxSelected = false;

            QString accessToken;
            emit requestRefreshAccessToken(target.account, target.email, &accessToken);
            if (accessToken.isEmpty()) {
                if (!accountChanged && !lastAccessToken.isEmpty()) {
                    accessToken = lastAccessToken;
                } else {
                    SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                             m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                             consecutiveFailures, "Realtime sync: auth refresh failed; retrying."_L1, 15);
                    continue;
                }
            }

            auto newCxn = std::make_shared<Connection>();
            const auto authMethod = target.authType == "password"_L1 ? AuthMethod::Login : AuthMethod::XOAuth2;
            const auto connectResult = newCxn->connectAndAuth(target.host, target.port, target.email, accessToken, authMethod);
            if (!connectResult.success) {
                if (!accountChanged && accessToken == lastAccessToken)
                    lastAccessToken.clear();
                SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                         m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                         consecutiveFailures, connectResult.message, 10);
                continue;
            }

            cxn = std::move(newCxn);
            lastEmail = target.email;
            lastAccessToken = accessToken;
        }

        // EXAMINE (read-only) is sufficient — IDLE only monitors for push notifications,
        // it never writes flags. Using EXAMINE avoids clearing \Recent on reconnect.
        if (!inboxSelected) {
            if (const auto result = cxn->examine("INBOX"_L1); !result) {
                cxn.reset();
                SyncUtils::handleFailure([this](bool ok2, const QString &msg) { emit realtimeStatus(ok2, msg); },
                                         m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                         consecutiveFailures, result.error(), 10);
                continue;
            }
            inboxSelected = true;
        }

        if (const auto result = cxn->enterIdle(); !result) {
            // Drop the connection — it may be half-open or stale.
            cxn.reset();
            inboxSelected = false;
            SyncUtils::handleFailure([this](bool ok2, const QString &msg) { emit realtimeStatus(ok2, msg); },
                                     m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                     consecutiveFailures, result.error(), 3);
            continue;
        }

        if (consecutiveFailures > 0) {
            consecutiveFailures = 0;
            if (m_realtimeDegradedNotified.exchange(false)) {
                SyncUtils::maybeEmitRealtime([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                             m_lastRealtimeStatusMs, true, "Realtime sync reconnected."_L1, 5000);
            }
        }

        const auto [mailboxChanged, existsSignals, recentSignals, expungeSignals] =
            waitForIdleSignals(*cxn, m_running);

        if (const auto doneResult = cxn->exitIdle(); !doneResult) {
            cxn.reset();
            inboxSelected = false;
            SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                     m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                     consecutiveFailures, doneResult.error(), 3);
            continue;
        }

        // After a clean IDLE exit the connection remains in the selected state —
        // no re-SELECT needed on the next iteration.

        if (!mailboxChanged) {
            QThread::sleep(1);
            continue;
        }

        const auto searchResp = cxn->execute("UID SEARCH ALL"_L1);
        const auto inboxUids = parseUidSearchAll(searchResp);

        const auto [newCount, deletedCount, maxLocalUidSeen] = reconcileInbox(this, target.email, inboxUids, maxUidWatermark);

        qInfo().noquote() << "[idle-cycle]" << "exists=" << existsSignals << "recent=" << recentSignals
                          << "expunge=" << expungeSignals << "remoteUids=" << inboxUids.size()
                          << "new=" << newCount << "deleted=" << deletedCount;

        if (deletedCount > 0) {
            SyncUtils::maybeEmitRealtime([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                         m_lastRealtimeStatusMs, true,
                                         QStringLiteral("%1 message%2 removed.")
                                             .arg(deletedCount)
                                             .arg(deletedCount == 1 ? QString() : "s"_L1),
                                         2000);
        }

        if (newCount > 0) {
            const auto priorHint = minUidHint.load();
            const auto monotonicHint = std::max(maxLocalUidSeen, priorHint);

            minUidHint.store(monotonicHint);

            if (monotonicHint > maxUidWatermark.load())
                maxUidWatermark.store(monotonicHint);

            qInfo().noquote() << "[idle-cycle] action=sync-inbox" << "minUidExclusive=" << monotonicHint;
            emit inboxChanged();
        } else {
            qInfo().noquote() << "[idle-cycle] action=prune-only";
        }
    }

    m_running = false;
}

} // namespace Imap
