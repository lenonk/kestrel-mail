#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <span>
#include <cstdint>

namespace Imap {

/**
 * Sync-related utility functions.
 */
namespace SyncUtils {

struct OAuthAccountTarget {
    QString email;
    QString host;
    qint32 port = 0;
    QVariantMap account;
};

/**
 * Default batch size for UID FETCH operations.
 */
constexpr int kSyncBatchSize = 100;

/**
 * Check if a sorted UID sequence is "fuzzy contiguous" - mostly sequential
 * with small gaps allowed. Used to decide between range vs explicit UID specs.
 * 
 * @param sortedUids Sorted UID array
 * @param maxTotalGaps Maximum total missing UIDs allowed (default: 8)
 * @param maxSingleGap Maximum single gap size allowed (default: 3)
 * @return true if sequence is fuzzy contiguous
 */
bool chunkIsFuzzyContiguous(std::span<const qint32> sortedUids,
                           int maxTotalGaps = 8, 
                           int maxSingleGap = 3);

/**
 * Get default recent fetch count from environment or default.
 * Reads KESTREL_RECENT_FETCH_COUNT, clamped to [20, 1000].
 */
int recentFetchCount();

// Shared OAuth account selection for sync workers.
std::tuple<bool, QString, OAuthAccountTarget> selectOAuthAccount(const QVariantList &accounts);

// Sleep in 1s chunks; early-exit if running becomes false.
void sleepInterruptible(std::atomic_bool &running, int totalSeconds);

// Throttled realtime status emitter.
void maybeEmitRealtime(const std::function<void(bool, const QString &)> &onRealtimeStatus,
                       std::atomic<qint64> &lastMs,
                       bool ok,
                       const QString &message,
                       int minIntervalMs);

// Shared degraded/retry handling helper.
void handleFailure(const std::function<void(bool, const QString &)> &onRealtimeStatus,
                   std::atomic<qint64> &lastMs,
                   std::atomic_bool &degradedNotified,
                   int &consecutiveFailures,
                   const QString &message,
                   int sleepSeconds,
                   std::atomic_bool *running = nullptr);

} // namespace SyncUtils
} // namespace Imap
