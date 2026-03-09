#pragma once

#include <atomic>

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace Imap {
class Connection;

class BackgroundWorker : public QObject {
    Q_OBJECT
public:
    explicit BackgroundWorker(QObject *parent = nullptr) : QObject(parent) {}

    [[nodiscard]] bool isRunning() const { return m_running.load(); }
    void setIntervalSeconds(const int seconds) { m_intervalSeconds = seconds; }

public slots:
    void start();
    void stop() { m_running.store(false); }

signals:
    void requestAccounts(QVariantList *out);
    void requestRefreshAccessToken(const QVariantMap &account, const QString &email, QString *out);

    void upsertFoldersRequested(const QVariantList &folders);
    void loadFolderStatusSnapshotRequested(const QString &accountEmail, const QString &folder,
                                           qint64 *uidNext, qint64 *highestModSeq, qint64 *messages, bool *found);
    void saveFolderStatusSnapshotRequested(const QString &accountEmail, const QString &folder,
                                           qint64 uidNext, qint64 highestModSeq, qint64 messages);
    void syncHeadersAndFlagsRequested(const QVariantMap &account, const QString &email, const QString &folder,
                                      const QString &accessToken);
    void fetchBodiesRequested(const QVariantMap &account, const QString &email, const QString &folder,
                              const QString &accessToken);
    void idleLiveUpdateRequested(const QVariantMap &account, const QString &email);

    void loopError(const QString &message);
    void realtimeStatus(bool ok, const QString &message);

private:
    std::atomic<qint64> m_lastRealtimeStatusMs      { 0 };
    std::atomic_bool    m_realtimeDegradedNotified  { false };
    std::atomic_bool    m_bootstrapped              { false };
    std::atomic_bool    m_running                   { false };

    int m_intervalSeconds = 120;

    QVariantMap m_activeAccount;
    QString m_activeEmail;
    QString m_activeAccessToken;

    struct FolderStatus {
        qint64 uidNext = -1;
        qint64 highestModSeq = -1;
        qint64 messages = -1;
    };

    void doBootstrap();
    std::pair<bool, QString> resolveAccount();
    FolderStatus fetchFolderStatus(const QString &folder) const;

    QHash<QString, FolderStatus> m_lastFolderStatus;
};

} // namespace Imap
