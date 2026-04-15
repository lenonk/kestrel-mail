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
                            const int operationalSlots, const int hydrateSlots) {
    if (m_initialized.exchange(true)) return;

    m_expectedPoolSize.store(operationalSlots + hydrateSlots);
    addSlots(email, host, port, method, credential, true,  hydrateSlots);
    addSlots(email, host, port, method, credential, false, operationalSlots);
}

void
ConnectionPool::addAccount(const QString &email, const QString &host, const int port,
                            const AuthMethod method, const QString &credential,
                            const int operationalSlots, const int hydrateSlots) {
    // Check if this email already has slots.
    {
        QMutexLocker lock(&m_poolMutex);
        for (const auto &s : m_poolSlots) {
            if (s.email.compare(email, Qt::CaseInsensitive) == 0)
                return;
        }
    }

    m_expectedPoolSize.fetch_add(operationalSlots + hydrateSlots);
    addSlots(email, host, port, method, credential, true,  hydrateSlots);
    addSlots(email, host, port, method, credential, false, operationalSlots);
}

void
ConnectionPool::addSlots(const QString &email, const QString &host, const int port,
                          const AuthMethod method, const QString &credential,
                          const bool isHydrate, const int count) {
    auto &slotVec = isHydrate ? m_hydrateSlots : m_poolSlots;
    QMutex *mtx = isHydrate ? &m_hydrateMutex : &m_poolMutex;
    QWaitCondition *cond = isHydrate ? &m_hydrateWait : &m_poolWait;

    for (int i = 0; i < count; ++i) {
        auto conn = std::make_unique<Connection>();
        conn->connectAndAuth(host, port, email, credential, method);
        Slot s;
        s.email = email; s.host = host; s.port = port; s.method = method;
        s.conn = std::move(conn); s.busy = false;
        QMutexLocker lock(mtx);
        slotVec.push_back(std::move(s));
        cond->wakeOne();
    }
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

    Connection *raw = m_poolSlots[slotIndex].conn.get();
    raw->setLogOwner(owner);
    const QString slotEmail = m_poolSlots[slotIndex].email;
    const QString slotHost  = m_poolSlots[slotIndex].host;
    const int     slotPort  = m_poolSlots[slotIndex].port;
    const auto    slotMethod = m_poolSlots[slotIndex].method;
    lock.unlock();

    if (!raw->isConnected()) {
        TokenRefresher refresher;
        {
            QMutexLocker rl(&m_poolMutex);
            refresher = m_tokenRefresher;
        }
        raw->setTokenRefresher(refresher);
        const QString token = refresher ? refresher(slotEmail) : QString{};
        if (!raw->connectAndAuth(slotHost, slotPort, slotEmail, token, slotMethod).success) {
            QMutexLocker rl(&m_poolMutex);
            m_poolSlots[slotIndex].busy = false;
            m_poolWait.wakeOne();
            return {};
        }
    }

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

std::shared_ptr<Connection>
ConnectionPool::acquireHydrate(const QString &email) {
    qint32 slotIndex = -1;
    {
        QMutexLocker lock(&m_hydrateMutex);
        for (qsizetype i = 0; i < static_cast<qsizetype>(m_hydrateSlots.size()); ++i) {
            if (!email.isEmpty() && m_hydrateSlots[i].email.compare(email, Qt::CaseInsensitive) != 0)
                continue;
            if (!m_hydrateSlots[i].busy) {
                m_hydrateSlots[i].busy = true;
                m_hydrateSlots[i].owner = "hydrate-dedicated"_L1;
                m_hydrateSlots[i].leasedAtMs = QDateTime::currentMSecsSinceEpoch();
                slotIndex = i;
                break;
            }
        }
    }
    if (slotIndex < 0) return {};

    Connection *raw = m_hydrateSlots[slotIndex].conn.get();
    raw->setLogOwner("hydrate-dedicated"_L1);
    const QString slotEmail  = m_hydrateSlots[slotIndex].email;
    const QString slotHost   = m_hydrateSlots[slotIndex].host;
    const int     slotPort   = m_hydrateSlots[slotIndex].port;
    const auto    slotMethod = m_hydrateSlots[slotIndex].method;

    if (!raw->isConnected()) {
        TokenRefresher refresher;
        {
            QMutexLocker rl(&m_poolMutex);
            refresher = m_tokenRefresher;
        }
        raw->setTokenRefresher(refresher);
        const QString token = refresher ? refresher(slotEmail) : QString{};
        if (!raw->connectAndAuth(slotHost, slotPort, slotEmail, token, slotMethod).success) {
            QMutexLocker lock(&m_hydrateMutex);
            m_hydrateSlots[slotIndex].busy = false;
            m_hydrateWait.wakeOne();
            return {};
        }
    }

    return {raw, [this, slotIndex](Connection *) {
        QMutexLocker rel(&m_hydrateMutex);
        if (slotIndex >= 0 && slotIndex < static_cast<qint32>(m_hydrateSlots.size())) {
            m_hydrateSlots[slotIndex].busy = false;
            m_hydrateSlots[slotIndex].owner.clear();
            m_hydrateSlots[slotIndex].leasedAtMs = 0;
            m_hydrateWait.wakeOne();
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
    qint32 count = 0;
    {
        QMutexLocker lock(&m_hydrateMutex);
        count += static_cast<qint32>(m_hydrateSlots.size());
    }
    {
        QMutexLocker lock(&m_poolMutex);
        count += static_cast<qint32>(m_poolSlots.size());
    }
    return count;
}

void
ConnectionPool::shutdown() {
    m_destroying.store(true);
    {
        QMutexLocker lock(&m_poolMutex);
        m_tokenRefresher = nullptr;
        for (auto &s : m_poolSlots) {
            if (s.conn) s.conn->disconnect();
        }
        m_poolSlots.clear();
        m_poolWait.wakeAll();
    }
    {
        QMutexLocker lock(&m_hydrateMutex);
        for (auto &s : m_hydrateSlots) {
            if (s.conn) s.conn->disconnect();
        }
        m_hydrateSlots.clear();
        m_hydrateWait.wakeAll();
    }
}

bool
ConnectionPool::isBackgroundOwner(const QString &owner) {
    return owner.startsWith("bg-"_L1, Qt::CaseInsensitive)
        || owner.startsWith("background"_L1, Qt::CaseInsensitive);
}

} // namespace Imap
