#pragma once

#include <atomic>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace Imap {

class IdleWatcher : public QObject {
    Q_OBJECT
public:
    explicit IdleWatcher(QObject *parent = nullptr) : QObject(parent) {}

    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    // Written by IDLE loop when new messages are detected.
    // Read/cleared by ImapService::syncFolder background path.
    std::atomic<qint64> minUidHint{0};
    std::atomic<qint64> maxUidWatermark{0};

public slots:
    void start();
    void stop() { m_running.store(false); }

signals:
    void requestAccounts(QVariantList *out);
    void requestRefreshAccessToken(const QVariantMap &account, const QString &email, QString *out);
    void requestFolderUids(const QString &email, const QString &folder, QStringList *out);
    void pruneFolderToUidsRequested(const QString &email, const QString &folder, const QStringList &uids);
    void removeUidsRequested(const QString &email, const QStringList &uids);
    void inboxChanged();
    void realtimeStatus(bool ok, const QString &message);

private:
    std::atomic_bool    m_running{false};
    std::atomic<qint64> m_lastRealtimeStatusMs{0};
    std::atomic_bool    m_realtimeDegradedNotified{false};
};

} // namespace Imap
