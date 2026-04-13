#include "syncutils.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QThread>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace Imap::SyncUtils {

bool
chunkIsFuzzyContiguous(const std::span<const int32_t> sortedUids, const qint32 maxTotalGaps, const qint32 maxSingleGap) {
    if (sortedUids.size() < 2)
        return true;

    qint32 totalMissing = 0;

    for (size_t i = 1; i < sortedUids.size(); ++i) {
        const auto delta = sortedUids[i] - sortedUids[i - 1];
        if (delta <= 1)
            continue;

        const auto gap = delta - 1;
        if (gap > maxSingleGap)
            return false;

        totalMissing += gap;
        if (totalMissing > maxTotalGaps)
            return false;
    }

    return true;
}

qint32
recentFetchCount() {
    auto ok = false;
    const auto value = qEnvironmentVariableIntValue("KESTREL_RECENT_FETCH_COUNT", &ok);

    if (ok) return value;

    return -1;
}

std::tuple<bool, QString, AccountTarget>
selectAccount(const QVariantList &accounts) {
    for (const auto &a : accounts) {
        const auto acc = a.toMap();
        const auto authType = acc.value("authType"_L1).toString();

        const bool isOAuth = authType == "oauth2"_L1
                          || (!acc.value("oauthClientId"_L1).toString().isEmpty()
                           && !acc.value("oauthTokenUrl"_L1).toString().isEmpty());
        const bool isPassword = authType == "password"_L1;

        if (!isOAuth && !isPassword)
            continue;

        const auto email = acc.value("email"_L1).toString();
        const auto host  = acc.value("imapHost"_L1).toString();
        const auto port  = acc.value("imapPort"_L1).toInt();

        if (email.isEmpty() || host.isEmpty() || port <= 0)
            return {false, "Realtime sync: account settings incomplete."_L1, {}};

        return {true, {}, {email, host, port, acc, isPassword ? "password"_L1 : "oauth2"_L1}};
    }

    return {false, "Realtime sync: no account available."_L1, {}};
}

void
sleepInterruptible(std::atomic_bool &running, const int totalSeconds) {
    if (totalSeconds <= 0)
        return;

    for (int i = 0; i < totalSeconds && running.load(); ++i)
        QThread::sleep(1);
}

void
maybeEmitRealtime(const std::function<void(bool, const QString &)> &onRealtimeStatus,
                  std::atomic<qint64> &lastMs,
                  const bool ok,
                  const QString &message,
                  const int minIntervalMs) {
    const auto now = QDateTime::currentMSecsSinceEpoch();

    if (const auto last = lastMs.load(); (now - last) < minIntervalMs)
        return;

    lastMs = now;

    if (onRealtimeStatus)
        onRealtimeStatus(ok, message);
}

void
handleFailure(const std::function<void(bool, const QString &)> &onRealtimeStatus,
              std::atomic<qint64> &lastMs,
              std::atomic_bool &degradedNotified,
              int &consecutiveFailures,
              const QString &message,
              const int sleepSeconds,
              std::atomic_bool *running) {
    ++consecutiveFailures;

    if (consecutiveFailures >= 3) {
        degradedNotified.store(true);
        maybeEmitRealtime(onRealtimeStatus, lastMs, false, message, 120000);
    }

    if (running) {
        sleepInterruptible(*running, sleepSeconds);
    } else {
        QThread::sleep(static_cast<unsigned long>(sleepSeconds));
    }
}

}
