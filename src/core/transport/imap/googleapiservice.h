#pragma once

#include <atomic>
#include <functional>
#include <QFutureWatcher>
#include <QMutex>
#include <QSet>
#include <QVariantList>

class DataStore;
class QFutureWatcherBase;

class GoogleApiService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList googleCalendarList READ googleCalendarList NOTIFY googleCalendarListChanged)
    Q_PROPERTY(QVariantList googleWeekEvents READ googleWeekEvents NOTIFY googleWeekEventsChanged)
    Q_PROPERTY(QVariantList googleContacts READ googleContacts NOTIFY googleContactsChanged)

public:
    using AccountConfigListFn  = std::function<QVariantList()>;
    using RefreshAccessTokenFn = std::function<QString(const QVariantMap &account, const QString &email)>;
    using PasswordLookupFn     = std::function<QString(const QString &email)>;

    GoogleApiService(AccountConfigListFn accountConfigList,
                     RefreshAccessTokenFn refreshAccessToken,
                     PasswordLookupFn passwordLookup,
                     DataStore *store,
                     QObject *parent = nullptr);
    ~GoogleApiService() override;

    [[nodiscard]] QVariantList googleCalendarList() const { return m_googleCalendarList; }
    [[nodiscard]] QVariantList googleWeekEvents() const { return m_googleWeekEvents; }
    [[nodiscard]] QVariantList googleContacts() const { return m_googleContacts; }

    Q_INVOKABLE void refreshGoogleCalendars();
    Q_INVOKABLE void refreshGoogleWeekEvents(const QStringList &calendarIds,
                                             const QString &weekStartIso,
                                             const QString &weekEndIso);
    Q_INVOKABLE void respondToCalendarInvite(const QString &calendarId,
                                             const QString &eventId,
                                             const QString &response);
    Q_INVOKABLE void refreshGoogleContacts();
    Q_INVOKABLE void refreshGooglePeopleAvatars(const QString &accountEmail);

    void shutdown();

signals:
    void googleCalendarListChanged();
    void googleWeekEventsChanged();
    void calendarInviteResponded();
    void googleContactsChanged();
    void statusMessage(bool ok, const QString &message);

private:
    struct AccountInfo {
        QString email;
        QString host;
        QString accessToken;
        int port = 0;
        QString authType;
    };

    [[nodiscard]] QList<AccountInfo> resolveAccounts(const QVariantList &accounts) const;
    [[nodiscard]] QString resolveGmailAccessToken(const QList<AccountInfo> &resolved) const;
    void runBackgroundTask(std::function<void()> task);
    void registerWatcher(QFutureWatcherBase *watcher);
    void unregisterWatcher(QFutureWatcherBase *watcher);

    AccountConfigListFn  m_accountConfigList;
    RefreshAccessTokenFn m_refreshAccessToken;
    PasswordLookupFn     m_passwordLookup;
    DataStore           *m_store = nullptr;

    QVariantList m_googleCalendarList;
    QVariantList m_googleWeekEvents;
    QVariantList m_googleContacts;

    std::atomic_bool m_destroying { false };

    QSet<QFutureWatcherBase*> m_activeWatchers;
    QMutex                    m_activeWatchersMutex;
};
