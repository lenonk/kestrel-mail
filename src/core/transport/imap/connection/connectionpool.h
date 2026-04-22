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
 * number of connections and hands them out via acquire(). Connections are
 * returned automatically when the shared_ptr's custom deleter runs.
 *
 * Background callers (owner starting with "bg-") are subject to a
 * reservation rule: when only one slot is free, it's held for foreground
 * operations (hydration, user-triggered sync, etc.).
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
                    int slotCount = 3);

    /// Add connections for a newly-added account (call when pool is already initialized).
    void addAccount(const QString &email, const QString &host, int port,
                    AuthMethod method, const QString &credential,
                    int slotCount = 3);

    /// Acquire a connection (blocks up to timeout).
    [[nodiscard]] std::shared_ptr<Connection> acquire(const QString &owner = {},
                                                       const QString &email = {},
                                                       int timeoutMs = 3500);

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

    /// Ping + reconnect a slot's connection. Returns false if reconnect fails.
    bool ensureConnected(Slot &slot);

    void addSlots(const QString &email, const QString &host, int port,
                  AuthMethod method, const QString &credential, int count);

    mutable QMutex m_poolMutex;
    QWaitCondition m_poolWait;
    std::vector<Slot> m_poolSlots;

    QSemaphore m_bgHydrateSem{1};
    QSemaphore m_bgFolderSyncSem{1};

    TokenRefresher m_tokenRefresher;
    std::atomic_bool m_initialized{false};
    std::atomic_bool m_destroying{false};
    std::atomic<qint32> m_expectedPoolSize{0};
};

} // namespace Imap
