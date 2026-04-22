#include "connectionpool.h"

#include <QDateTime>

using namespace Qt::Literals::StringLiterals;

namespace Imap {

ConnectionPool::~ConnectionPool() { shutdown(); }

void
ConnectionPool::setTokenRefresher(TokenRefresher refresher) {
    QMutexLocker lock(&m_poolMutex);
    m_tokenRefresher = std::move(refresher);
}

void
ConnectionPool::initialize(const QString &email, const QString &host, const int port,
                            const AuthMethod method, const QString &credential,
                            const int slotCount) {
    if (m_initialized.exchange(true)) return;
    m_expectedPoolSize.store(slotCount);
    addSlots(email, host, port, method, credential, slotCount);
}

void
ConnectionPool::addAccount(const QString &email, const QString &host, const int port,
                            const AuthMethod method, const QString &credential,
                            const int slotCount) {
    {
        QMutexLocker lock(&m_poolMutex);
        for (const auto &s : m_poolSlots) {
            if (s.email.compare(email, Qt::CaseInsensitive) == 0)
                return;
        }
    }

    m_expectedPoolSize.fetch_add(slotCount);
    addSlots(email, host, port, method, credential, slotCount);
}

void
ConnectionPool::addSlots(const QString &email, const QString &host, const int port,
                          const AuthMethod method, const QString &credential,
                          const int count) {
    for (int i = 0; i < count; ++i) {
        auto conn = std::make_unique<Connection>();
        conn->connectAndAuth(host, port, email, credential, method);
        Slot s;
        s.email = email; s.host = host; s.port = port; s.method = method;
        s.conn = std::move(conn); s.busy = false;
        QMutexLocker lock(&m_poolMutex);
        m_poolSlots.push_back(std::move(s));
        m_poolWait.wakeOne();
    }
}

bool
ConnectionPool::ensureConnected(Slot &slot) {
    Connection *raw = slot.conn.get();

    // Fast liveness check: ping detects silently dropped connections
    // in ~3s instead of waiting 12s for the next command to time out.
    if (raw->isConnected() && !raw->ping())
        raw->disconnect();

    if (!raw->isConnected()) {
        TokenRefresher refresher;
        {
            QMutexLocker rl(&m_poolMutex);
            refresher = m_tokenRefresher;
        }
        raw->setTokenRefresher(refresher);
        const QString token = refresher ? refresher(slot.email) : QString{};
        return raw->connectAndAuth(slot.host, slot.port, slot.email, token, slot.method).success;
    }
    return true;
}

std::shared_ptr<Connection>
ConnectionPool::acquire(const QString &owner, const QString &email, const int timeoutMs) {
    qint32 slotIndex = -1;
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    QMutexLocker lock(&m_poolMutex);
    while (true) {
        if (m_destroying.load()) return {};

        qint32 freeForEmail = 0;
        for (qsizetype i = 0; i < static_cast<qsizetype>(m_poolSlots.size()); ++i) {
            if (!email.isEmpty() && m_poolSlots[i].email.compare(email, Qt::CaseInsensitive) != 0)
                continue;
            if (!m_poolSlots[i].busy)
                ++freeForEmail;
        }

        const bool reserveForUser = isBackgroundOwner(owner) && freeForEmail <= 1;

        if (!reserveForUser) {
            for (qsizetype i = 0; i < static_cast<qsizetype>(m_poolSlots.size()); ++i) {
                if (m_poolSlots[i].busy) continue;
                if (!email.isEmpty() && m_poolSlots[i].email.compare(email, Qt::CaseInsensitive) != 0)
                    continue;
                m_poolSlots[i].busy = true;
                m_poolSlots[i].owner = owner;
                m_poolSlots[i].leasedAtMs = QDateTime::currentMSecsSinceEpoch();
                slotIndex = i;
                break;
            }
        }

        if (slotIndex >= 0) break;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint32 remaining = static_cast<qint32>(deadline - now);
        if (remaining <= 0) return {};
        m_poolWait.wait(&m_poolMutex, remaining);
    }

    m_poolSlots[slotIndex].conn->setLogOwner(owner);
    lock.unlock();

    if (!ensureConnected(m_poolSlots[slotIndex])) {
        QMutexLocker rl(&m_poolMutex);
        m_poolSlots[slotIndex].busy = false;
        m_poolWait.wakeOne();
        return {};
    }

    Connection *raw = m_poolSlots[slotIndex].conn.get();
    return {raw, [this, slotIndex](Connection *) {
        QMutexLocker rel(&m_poolMutex);
        if (slotIndex >= 0 && slotIndex < static_cast<qint32>(m_poolSlots.size())) {
            m_poolSlots[slotIndex].busy = false;
            m_poolSlots[slotIndex].owner.clear();
            m_poolSlots[slotIndex].leasedAtMs = 0;
            m_poolWait.wakeOne();
        }
    }};
}

bool ConnectionPool::tryAcquireBgHydrate(const int timeoutMs) { return m_bgHydrateSem.tryAcquire(1, timeoutMs); }
void ConnectionPool::releaseBgHydrate() { m_bgHydrateSem.release(); }
bool ConnectionPool::tryAcquireBgFolderSync(const int timeoutMs) { return m_bgFolderSyncSem.tryAcquire(1, timeoutMs); }
void ConnectionPool::releaseBgFolderSync() { m_bgFolderSyncSem.release(); }

qint32
ConnectionPool::expectedConnections() const { return m_expectedPoolSize.load(); }

qint32
ConnectionPool::readyConnections() const {
    QMutexLocker lock(&m_poolMutex);
    return static_cast<qint32>(m_poolSlots.size());
}

void
ConnectionPool::shutdown() {
    m_destroying.store(true);
    QMutexLocker lock(&m_poolMutex);
    m_tokenRefresher = nullptr;
    for (auto &s : m_poolSlots) {
        if (s.conn) s.conn->disconnect();
    }
    m_poolSlots.clear();
    m_poolWait.wakeAll();
}

bool
ConnectionPool::isBackgroundOwner(const QString &owner) {
    return owner.startsWith("bg-"_L1, Qt::CaseInsensitive)
        || owner.startsWith("background"_L1, Qt::CaseInsensitive);
}

} // namespace Imap
