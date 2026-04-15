#pragma once

#include "imapconnection.h"

#include <QMutex>
#include <QSemaphore>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace Imap {

/**
 * Thread-safe pool of IMAP connections for a single account.
 *
 * Each account owns its own ConnectionPool. The pool pre-creates a fixed
 * number of connections (operational + hydration) and hands them out via
 * acquire()/acquireHydrate(). Connections are returned automatically when
 * the shared_ptr's custom deleter runs.
 */
class ConnectionPool
{
public:
    struct Slot {
        std::unique_ptr<Connection> conn;
        bool busy = false;
        QString email;
        QString host;
        int port = 0;
        AuthMethod method = AuthMethod::XOAuth2;
        QString owner;
        qint64 leasedAtMs = 0;
    };

    using TokenRefresher = std::function<QString(const QString &email)>;

    ConnectionPool() = default;
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool &) = delete;
    ConnectionPool &operator=(const ConnectionPool &) = delete;

    void setTokenRefresher(TokenRefresher refresher);

    /// Pre-create connections for the given account.
    void initialize(const QString &email, const QString &host, int port,
                    AuthMethod method, const QString &credential,
                    int operationalSlots = 3, int hydrateSlots = 1);

    /// Add connections for a newly-added account (call when pool is already initialized).
    void addAccount(const QString &email, const QString &host, int port,
                    AuthMethod method, const QString &credential,
                    int operationalSlots = 3, int hydrateSlots = 1);

    /// Acquire an operational connection (blocks up to timeout).
    [[nodiscard]] std::shared_ptr<Connection> acquire(const QString &owner = {},
                                                       const QString &email = {},
                                                       int timeoutMs = 3500);

    /// Acquire a dedicated hydration connection (non-blocking).
    [[nodiscard]] std::shared_ptr<Connection> acquireHydrate(const QString &email = {});

    /// Background hydration semaphore (limits concurrent background hydrations to 1).
    bool tryAcquireBgHydrate(int timeoutMs = 60000);
    void releaseBgHydrate();

    /// Background folder-sync semaphore (limits concurrent background syncs to 1).
    bool tryAcquireBgFolderSync(int timeoutMs = 30000);
    void releaseBgFolderSync();

    [[nodiscard]] qint32 expectedConnections() const;
    [[nodiscard]] qint32 readyConnections() const;

    void shutdown();

private:
    static bool isBackgroundOwner(const QString &owner);

    /// Create `count` connections and push them into the hydrate or operational pool.
    void addSlots(const QString &email, const QString &host, int port,
                  AuthMethod method, const QString &credential,
                  bool isHydrate, int count);

    mutable QMutex m_poolMutex;
    QWaitCondition m_poolWait;
    std::vector<Slot> m_poolSlots;

    mutable QMutex m_hydrateMutex;
    QWaitCondition m_hydrateWait;
    std::vector<Slot> m_hydrateSlots;

    QSemaphore m_bgHydrateSem{1};
    QSemaphore m_bgFolderSyncSem{1};

    TokenRefresher m_tokenRefresher;
    std::atomic_bool m_initialized{false};
    std::atomic_bool m_destroying{false};
    std::atomic<qint32> m_expectedPoolSize{0};
};

} // namespace Imap
