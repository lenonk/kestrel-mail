#include "backgroundworker.h"

#include "syncutils.h"
#include "src/core/transport/imap/connection/imapconnection.h"

using namespace Qt::Literals::StringLiterals;

namespace Imap {

std::pair<bool, QString>
BackgroundWorker::connectAndAuth() {
    static int consecutiveFailures = 0;

    m_cxn = std::make_unique<Connection>();

    QVariantList accounts;
    emit requestAccounts(&accounts);

    const auto [accountOk, accountErr, target] = SyncUtils::selectOAuthAccount(accounts);
    if (!accountOk) {
        SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                 m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                 consecutiveFailures, accountErr, 10, &m_running);
        return {false, accountErr};
    }

    QString accessToken;
    emit requestRefreshAccessToken(target.account, target.email, &accessToken);

    if (accessToken.isEmpty()) {
        constexpr auto err = "Realtime sync: auth refresh failed; retrying."_L1;
        SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                 m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                 consecutiveFailures, err, 15, &m_running);
        return {false, err};
    }

    const auto connectResult = m_cxn->connectAndAuth(target.host, target.port, target.email, accessToken);
    if (!connectResult.success) {
        SyncUtils::handleFailure([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                 m_lastRealtimeStatusMs, m_realtimeDegradedNotified,
                                 consecutiveFailures, connectResult.message, 10, &m_running);
        return {false, connectResult.message};
    }

    m_activeAccount = target.account;
    m_activeEmail = target.email;
    m_activeAccessToken = accessToken;

    consecutiveFailures = 0;
    return {true, {}};
}

void
BackgroundWorker::doBootstrap() {
    emit loginSessionStartupRequested(m_activeAccount, m_activeEmail, m_activeAccessToken);
    m_bootstrapped = true;
}

void
BackgroundWorker::start() {
    if (m_running.exchange(true))
        return;

    while (m_running.load()) {
        if (const auto [ok, err] = connectAndAuth(); !ok) {
            emit loopError(err);
            continue;
        }

        if (!m_bootstrapped.load())
            doBootstrap();

        QStringList folders;
        emit listFoldersRequested(m_activeAccount, m_activeEmail, m_activeAccessToken, &folders);

        for (const auto &folder : folders) {
            if (!m_running.load())
                break;

            bool shouldSync = true;
            emit shouldSyncFolderRequested(m_activeAccount, m_activeEmail, folder, m_activeAccessToken, &shouldSync);
            if (!shouldSync)
                continue;

            emit syncHeadersAndFlagsRequested(m_activeAccount, m_activeEmail, folder, m_activeAccessToken);
            emit fetchBodiesRequested(m_activeAccount, m_activeEmail, folder, m_activeAccessToken);
        }

        emit idleLiveUpdateRequested(m_activeAccount, m_activeEmail);

        if (m_realtimeDegradedNotified.load()) {
            m_realtimeDegradedNotified = false;
            SyncUtils::maybeEmitRealtime([this](bool ok, const QString &msg) { emit realtimeStatus(ok, msg); },
                                         m_lastRealtimeStatusMs, true, "Realtime sync recovered."_L1, 0);
        }

        SyncUtils::sleepInterruptible(m_running, m_intervalSeconds);
    }

    if (m_cxn)
        m_cxn->disconnect();

    m_cxn.reset();
    m_running = false;
}

} // namespace Imap
