#include "googleapiservice.h"

#include "../../store/datastore.h"
#include "message/avatarresolver.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtConcurrentRun>
#include <algorithm>
#include <ranges>

using namespace Qt::Literals::StringLiterals;
using Imap::AvatarResolver::resolveGooglePeopleAvatarUrl;
using Imap::AvatarResolver::fetchAvatarBlob;
using Imap::AvatarResolver::writeAvatarFile;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GoogleApiService::GoogleApiService(AccountConfigListFn accountConfigList,
                                   RefreshAccessTokenFn refreshAccessToken,
                                   PasswordLookupFn passwordLookup,
                                   DataStore *store,
                                   QObject *parent)
    : QObject(parent)
    , m_accountConfigList(std::move(accountConfigList))
    , m_refreshAccessToken(std::move(refreshAccessToken))
    , m_passwordLookup(std::move(passwordLookup))
    , m_store(store)
{
}

GoogleApiService::~GoogleApiService()
{
    shutdown();
}

void
GoogleApiService::shutdown() {
    m_destroying.store(true);

    QMutexLocker lock(&m_activeWatchersMutex);
    for (auto *w : m_activeWatchers)
        w->waitForFinished();
    m_activeWatchers.clear();
}

// ---------------------------------------------------------------------------
// Watcher lifecycle
// ---------------------------------------------------------------------------

void
GoogleApiService::registerWatcher(QFutureWatcherBase *watcher) {
    QMutexLocker lock(&m_activeWatchersMutex);
    m_activeWatchers.insert(watcher);
}

void
GoogleApiService::unregisterWatcher(QFutureWatcherBase *watcher) {
    QMutexLocker lock(&m_activeWatchersMutex);
    m_activeWatchers.remove(watcher);
}

void
GoogleApiService::runBackgroundTask(std::function<void()> task) {
    auto *watcher = new QFutureWatcher<void>(this);
    registerWatcher(watcher);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        unregisterWatcher(watcher);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::move(task)));
}

// ---------------------------------------------------------------------------
// Account resolution
// ---------------------------------------------------------------------------

QList<GoogleApiService::AccountInfo>
GoogleApiService::resolveAccounts(const QVariantList &accounts) const {
    QList<AccountInfo> result;
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
        const int  port  = acc.value("imapPort"_L1).toInt();

        if (email.isEmpty() || host.isEmpty() || port <= 0)
            continue;

        QString credential;
        if (isOAuth) {
            credential = m_refreshAccessToken(acc, email);
        } else if (m_passwordLookup) {
            credential = m_passwordLookup(email);
        }

        if (credential.isEmpty())
            continue;

        result.push_back({email, host, credential, port, isPassword ? "password"_L1 : "oauth2"_L1});
    }

    return result;
}

QString
GoogleApiService::resolveGmailAccessToken(const QList<AccountInfo> &resolved) const {
    auto it = std::ranges::find_if(resolved, [](const AccountInfo &info) {
        return info.host.contains("gmail", Qt::CaseInsensitive);
    });
    if (it == resolved.end())
        return {};
    return it->accessToken;
}

// ---------------------------------------------------------------------------
// Google Calendar: list
// ---------------------------------------------------------------------------

void
GoogleApiService::refreshGoogleCalendars() {
    if (m_destroying)
        return;

    const auto accounts = m_accountConfigList();
    if (accounts.isEmpty()) {
        if (!m_googleCalendarList.isEmpty()) {
            m_googleCalendarList.clear();
            emit googleCalendarListChanged();
        }
        return;
    }

    runBackgroundTask([this, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        const QString accessToken = resolveGmailAccessToken(resolved);
        if (accessToken.isEmpty())
            return;

        QNetworkAccessManager nam;
        QUrl listUrl("https://www.googleapis.com/calendar/v3/users/me/calendarList"_L1);
        QUrlQuery listQuery;
        listQuery.addQueryItem("colorRgbFormat"_L1, "true"_L1);
        listUrl.setQuery(listQuery);
        QNetworkRequest req{listUrl};
        req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

        QEventLoop loop;
        QNetworkReply *reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray payload = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString err = reply->errorString();
        const qint32 httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString bodyText = QString::fromUtf8(payload).trimmed();
        reply->deleteLater();
        if (!ok) {
            qWarning().noquote() << "[calendar][google] calendarList fetch failed"
                                 << "httpStatus=" << httpStatus
                                 << "error=" << err
                                 << "body=" << bodyText;
            QMetaObject::invokeMethod(this, [this, httpStatus]() {
                Q_UNUSED(httpStatus)
                emit statusMessage(false, "Calendar sync error. Please reconnect your Google account and try again."_L1);
            }, Qt::QueuedConnection);
            return;
        }

        const auto doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            return;

        QVariantList list;
        const auto items = doc.object().value("items").toArray();
        list.reserve(items.size());
        for (const auto &v : items) {
            const auto o = v.toObject();
            QVariantMap row;
            row.insert("id", o.value("id").toString());
            row.insert("name", o.value("summary").toString());
            row.insert("color", o.value("backgroundColor").toString());
            row.insert("checked", true);
            row.insert("account", "gmail");
            list.push_back(row);
        }

        QMetaObject::invokeMethod(this, [this, list]() {
            m_googleCalendarList = list;
            emit googleCalendarListChanged();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Google Calendar: week events
// ---------------------------------------------------------------------------

void
GoogleApiService::refreshGoogleWeekEvents(const QStringList &calendarIds,
                                          const QString &weekStartIso,
                                          const QString &weekEndIso) {
    if (m_destroying)
        return;

    const auto accounts = m_accountConfigList();
    if (accounts.isEmpty() || calendarIds.isEmpty()) {
        if (!m_googleWeekEvents.isEmpty()) {
            m_googleWeekEvents.clear();
            emit googleWeekEventsChanged();
        }
        return;
    }

    // Build calendarId -> backgroundColor map from the cached calendar list.
    QHash<QString, QString> calendarColorMap;
    for (const auto &entry : m_googleCalendarList) {
        const auto m = entry.toMap();
        calendarColorMap.insert(m.value("id"_L1).toString(),
                                m.value("color"_L1).toString());
    }

    runBackgroundTask([this, accounts, calendarIds, weekStartIso, weekEndIso, calendarColorMap]() {
        const auto resolved = resolveAccounts(accounts);
        const QString accessToken = resolveGmailAccessToken(resolved);
        if (accessToken.isEmpty())
            return;

        const QDateTime weekStart = QDateTime::fromString(weekStartIso, Qt::ISODate);
        const QDateTime weekEnd = QDateTime::fromString(weekEndIso, Qt::ISODate);
        if (!weekStart.isValid() || !weekEnd.isValid())
            return;

        const qint32 totalDays = static_cast<qint32>(weekStart.date().daysTo(weekEnd.date())) - 1;

        QNetworkAccessManager nam;
        QVariantList out;

        for (const QString &calendarId : calendarIds) {
            QUrl evUrl("https://www.googleapis.com/calendar/v3/calendars/%1/events"_L1
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(calendarId))));
            QUrlQuery q;
            q.addQueryItem("singleEvents"_L1, "true"_L1);
            q.addQueryItem("orderBy"_L1, "startTime"_L1);
            q.addQueryItem("timeMin"_L1, weekStart.toUTC().toString(Qt::ISODate));
            q.addQueryItem("timeMax"_L1, weekEnd.toUTC().toString(Qt::ISODate));
            evUrl.setQuery(q);

            QNetworkRequest req{evUrl};
            req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

            QEventLoop loop;
            QNetworkReply *reply = nam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            const qint32 httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString bodyText = QString::fromUtf8(payload).trimmed();
            reply->deleteLater();
            if (!ok) {
                qWarning().noquote() << "[calendar][google] events fetch failed"
                                     << "calendarId=" << calendarId
                                     << "httpStatus=" << httpStatus
                                     << "error=" << err
                                     << "body=" << bodyText;
                QMetaObject::invokeMethod(this, [this, calendarId]() {
                    Q_UNUSED(calendarId)
                    emit statusMessage(false, "Calendar events couldn't be loaded right now. Please try again."_L1);
                }, Qt::QueuedConnection);
                continue;
            }

            const auto doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject())
                continue;

            const auto items = doc.object().value("items").toArray();
            for (const auto &v : items) {
                const auto o = v.toObject();
                const QString eventId = o.value("id").toString();
                const auto startObj = o.value("start").toObject();
                const auto endObj = o.value("end").toObject();

                const QString startDateTimeStr = startObj.value("dateTime").toString();
                const QString endDateTimeStr = endObj.value("dateTime").toString();
                const QString startDateStr = startObj.value("date").toString();
                const QString endDateStr = endObj.value("date").toString();

                QDateTime startDt;
                QDateTime endDt;
                bool isAllDay = false;

                if (!startDateTimeStr.isEmpty() && !endDateTimeStr.isEmpty()) {
                    startDt = QDateTime::fromString(startDateTimeStr, Qt::ISODate);
                    endDt = QDateTime::fromString(endDateTimeStr, Qt::ISODate);
                } else if (!startDateStr.isEmpty() && !endDateStr.isEmpty()) {
                    // Google all-day end.date is exclusive.
                    const QDate s = QDate::fromString(startDateStr, Qt::ISODate);
                    const QDate eExclusive = QDate::fromString(endDateStr, Qt::ISODate);
                    if (s.isValid() && eExclusive.isValid()) {
                        isAllDay = true;
                        const QTimeZone localTz = QTimeZone::systemTimeZone();
                        startDt = QDateTime(s, QTime(0, 0, 0), localTz);
                        endDt = QDateTime(eExclusive, QTime(0, 0, 0), localTz);
                    }
                }

                if (!startDt.isValid() || !endDt.isValid())
                    continue;

                // Per-event color: use event's own backgroundColor if present, else the calendar's color.
                QString eventColor = o.value("backgroundColor").toString();
                if (eventColor.isEmpty())
                    eventColor = calendarColorMap.value(calendarId);

                // Recurrence description (from the expanded instance's recurringEventId).
                const QString recurrence = o.contains("recurringEventId")
                                           ? o.value("recurrence").toArray().isEmpty()
                                             ? "Recurring"_L1
                                             : o.value("recurrence").toArray().first().toString()
                                           : QString();

                // Organizer display name or email, and whether this is someone else's event.
                const auto organizerObj = o.value("organizer").toObject();
                const QString organizer = organizerObj.value("displayName").toString().isEmpty()
                    ? organizerObj.value("email").toString()
                    : organizerObj.value("displayName").toString();
                const bool organizerIsSelf = organizerObj.value("self").toBool(false);

                // Find the current user's response status from attendees.
                QString selfResponseStatus;
                const auto attendees = o.value("attendees").toArray();
                for (const auto &att : attendees) {
                    const auto a = att.toObject();
                    if (a.value("self").toBool(false)) {
                        selfResponseStatus = a.value("responseStatus").toString();
                        break;
                    }
                }

                // ISO start time for QML sorting/formatting.
                const QString startIso = startDt.toString(Qt::ISODate);

                if (isAllDay) {
                    const qint64 rawStart = weekStart.date().daysTo(startDt.date());
                    const qint64 rawEnd   = weekStart.date().daysTo(endDt.date().addDays(-1));
                    const qint32 visStart = qMax(0, static_cast<qint32>(rawStart));
                    const qint32 visEnd   = qMin(totalDays, static_cast<qint32>(rawEnd));
                    if (visStart > totalDays || visEnd < 0)
                        continue;

                    QVariantMap row;
                    row.insert("eventId"_L1, eventId);
                    row.insert("calendarId"_L1, calendarId);
                    row.insert("dayIndex"_L1, visStart);
                    row.insert("spanDays"_L1, visEnd - visStart + 1);
                    row.insert("startHour"_L1, 0.0);
                    row.insert("durationHours"_L1, 24.0);
                    row.insert("isAllDay"_L1, true);
                    row.insert("title"_L1, o.value("summary"_L1).toString());
                    row.insert("subtitle"_L1, "All day"_L1);
                    row.insert("color"_L1, eventColor);
                    row.insert("location"_L1, o.value("location"_L1).toString());
                    row.insert("visibility"_L1, o.value("visibility"_L1).toString());
                    row.insert("recurrence"_L1, recurrence);
                    row.insert("organizer"_L1, organizer);
                    row.insert("organizerIsSelf"_L1, organizerIsSelf);
                    row.insert("selfResponseStatus"_L1, selfResponseStatus);
                    row.insert("startIso"_L1, startIso);
                    out.push_back(row);
                } else {
                    const qint64 dayIndex = weekStart.date().daysTo(startDt.date());
                    if (dayIndex < 0 || dayIndex > totalDays)
                        continue;

                    const qint32 minutes = startDt.time().hour() * 60 + startDt.time().minute();
                    const qint32 durMinutes = qMax(15, static_cast<qint32>(startDt.secsTo(endDt) / 60));

                    QVariantMap row;
                    row.insert("eventId"_L1, eventId);
                    row.insert("calendarId"_L1, calendarId);
                    row.insert("dayIndex"_L1, static_cast<qint32>(dayIndex));
                    row.insert("spanDays"_L1, 1);
                    row.insert("startHour"_L1, static_cast<double>(minutes) / 60.0);
                    row.insert("durationHours"_L1, static_cast<double>(durMinutes) / 60.0);
                    row.insert("isAllDay"_L1, false);
                    row.insert("title"_L1, o.value("summary"_L1).toString());
                    row.insert("subtitle"_L1, "%1 - %2"_L1
                                     .arg(startDt.time().toString("h:mmap"_L1).toLower())
                                     .arg(endDt.time().toString("h:mmap"_L1).toLower()));
                    row.insert("color"_L1, eventColor);
                    row.insert("location"_L1, o.value("location"_L1).toString());
                    row.insert("visibility"_L1, o.value("visibility"_L1).toString());
                    row.insert("recurrence"_L1, recurrence);
                    row.insert("organizer"_L1, organizer);
                    row.insert("organizerIsSelf"_L1, organizerIsSelf);
                    row.insert("selfResponseStatus"_L1, selfResponseStatus);
                    row.insert("startIso"_L1, startIso);
                    out.push_back(row);
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, out]() {
            m_googleWeekEvents = out;
            emit googleWeekEventsChanged();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Google Contacts
// ---------------------------------------------------------------------------

void
GoogleApiService::refreshGoogleContacts() {
    if (m_destroying)
        return;

    const auto accounts = m_accountConfigList();
    if (accounts.isEmpty())
        return;

    runBackgroundTask([this, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        const QString accessToken = resolveGmailAccessToken(resolved);
        if (accessToken.isEmpty())
            return;

        QNetworkAccessManager nam;
        QVariantList out;
        QString nextPageToken;

        do {
            QUrl url("https://people.googleapis.com/v1/people/me/connections"_L1);
            QUrlQuery q;
            q.addQueryItem("personFields"_L1, "names,emailAddresses,phoneNumbers,photos,organizations"_L1);
            q.addQueryItem("pageSize"_L1, "1000"_L1);
            q.addQueryItem("sortOrder"_L1, "LAST_NAME_ASCENDING"_L1);
            if (!nextPageToken.isEmpty())
                q.addQueryItem("pageToken"_L1, nextPageToken);
            url.setQuery(q);

            QNetworkRequest req{url};
            req.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());

            QEventLoop loop;
            QNetworkReply *reply = nam.get(req);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            const QByteArray payload = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!ok) {
                qWarning() << "[contacts][google] fetch failed";
                break;
            }

            const auto doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject()) { break; }

            const auto connections = doc.object().value("connections").toArray();
            for (const auto &v : connections) {
                const auto p = v.toObject();

                const auto names = p.value("names").toArray();
                const auto emails = p.value("emailAddresses").toArray();
                const auto phones = p.value("phoneNumbers").toArray();
                const auto photos = p.value("photos").toArray();
                const auto orgs = p.value("organizations").toArray();

                QString displayName;
                QString givenName;
                QString familyName;
                if (!names.isEmpty()) {
                    const auto n = names[0].toObject();
                    displayName = n.value("displayName").toString();
                    givenName = n.value("givenName").toString();
                    familyName = n.value("familyName").toString();
                }
                if (displayName.isEmpty()) { continue; }

                QVariantList emailList;
                for (const auto &e : emails) {
                    const auto eo = e.toObject();
                    emailList.push_back(QVariantMap{
                        {"value"_L1, eo.value("value").toString()},
                        {"type"_L1, eo.value("type").toString()}
                    });
                }

                QVariantList phoneList;
                for (const auto &ph : phones) {
                    const auto po = ph.toObject();
                    phoneList.push_back(QVariantMap{
                        {"value"_L1, po.value("value").toString()},
                        {"type"_L1, po.value("type").toString()}
                    });
                }

                QString photoUrl;
                if (!photos.isEmpty()) {
                    const auto photo = photos[0].toObject();
                    if (!photo.value("default").toBool(false))
                        photoUrl = photo.value("url").toString();
                }

                QString organization;
                QString title;
                if (!orgs.isEmpty()) {
                    const auto org = orgs[0].toObject();
                    organization = org.value("name").toString();
                    title = org.value("title").toString();
                }

                QVariantMap row;
                row.insert("resourceName"_L1, p.value("resourceName").toString());
                row.insert("displayName"_L1, displayName);
                row.insert("givenName"_L1, givenName);
                row.insert("familyName"_L1, familyName);
                row.insert("emails"_L1, emailList);
                row.insert("phones"_L1, phoneList);
                row.insert("photoUrl"_L1, photoUrl);
                row.insert("organization"_L1, organization);
                row.insert("title"_L1, title);
                out.push_back(row);
            }

            nextPageToken = doc.object().value("nextPageToken").toString();
        } while (!nextPageToken.isEmpty());

        std::ranges::sort(out, [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("displayName"_L1).toString()
                       .compare(b.toMap().value("displayName"_L1).toString(), Qt::CaseInsensitive) < 0;
        });

        qInfo() << "[contacts][google] fetched" << out.size() << "contacts";

        QMetaObject::invokeMethod(this, [this, out]() {
            m_googleContacts = out;
            emit googleContactsChanged();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Calendar invite RSVP
// ---------------------------------------------------------------------------

void
GoogleApiService::respondToCalendarInvite(const QString &calendarId,
                                          const QString &eventId,
                                          const QString &response) {
    if (calendarId.isEmpty() || eventId.isEmpty() || response.isEmpty())
        return;

    const auto accounts = m_accountConfigList();
    runBackgroundTask([this, calendarId, eventId, response, accounts]() {
        const auto resolved = resolveAccounts(accounts);
        const QString accessToken = resolveGmailAccessToken(resolved);
        if (accessToken.isEmpty())
            return;

        const QString encodedCalId = QString::fromUtf8(QUrl::toPercentEncoding(calendarId));
        const QString encodedEvtId = QString::fromUtf8(QUrl::toPercentEncoding(eventId));

        // GET the event to retrieve current attendees.
        QUrl getUrl("https://www.googleapis.com/calendar/v3/calendars/%1/events/%2"_L1
                    .arg(encodedCalId, encodedEvtId));

        QNetworkAccessManager nam;
        QNetworkRequest getReq{getUrl};
        getReq.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());

        QEventLoop loop;
        QNetworkReply *getReply = nam.get(getReq);
        QObject::connect(getReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const QByteArray getPayload = getReply->readAll();
        const bool getOk = getReply->error() == QNetworkReply::NoError;
        getReply->deleteLater();
        if (!getOk) {
            qWarning() << "[calendar] GET event failed for RSVP:" << eventId;
            return;
        }

        auto eventObj = QJsonDocument::fromJson(getPayload).object();
        auto attendees = eventObj.value("attendees").toArray();

        // Update self's responseStatus.
        bool found = false;
        for (qsizetype i = 0; i < attendees.size(); ++i) {
            auto a = attendees[i].toObject();
            if (a.value("self").toBool(false)) {
                a["responseStatus"] = response;
                attendees[i] = a;
                found = true;
                break;
            }
        }
        if (!found) {
            qWarning() << "[calendar] Self not found in attendees for" << eventId;
            return;
        }

        // PATCH the event with updated attendees.
        QJsonObject patchBody;
        patchBody["attendees"] = attendees;

        QUrl patchUrl(getUrl);
        QUrlQuery pq;
        pq.addQueryItem("sendUpdates"_L1, "all"_L1);
        patchUrl.setQuery(pq);

        QNetworkRequest patchReq{patchUrl};
        patchReq.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
        patchReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_L1);

        QNetworkReply *patchReply = nam.sendCustomRequest(
            patchReq, "PATCH", QJsonDocument(patchBody).toJson(QJsonDocument::Compact));
        QObject::connect(patchReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const bool patchOk = patchReply->error() == QNetworkReply::NoError;
        patchReply->deleteLater();

        if (patchOk) {
            qInfo() << "[calendar] RSVP sent:" << response << "for" << eventId;
            QMetaObject::invokeMethod(this, [this]() {
                emit calendarInviteResponded();
            }, Qt::QueuedConnection);
        } else {
            qWarning() << "[calendar] PATCH event failed for RSVP:" << eventId;
        }
    });
}

// ---------------------------------------------------------------------------
// Google People avatars
// ---------------------------------------------------------------------------

void
GoogleApiService::refreshGooglePeopleAvatars(const QString &accountEmail) {
    if (m_destroying || !m_store) return;

    const auto accounts = m_accountConfigList();
    runBackgroundTask([this, accounts, accountEmail]() {
        const auto resolved = resolveAccounts(accounts);
        auto it = std::ranges::find_if(resolved, [&](const AccountInfo &info) {
            return info.email.compare(accountEmail, Qt::CaseInsensitive) == 0;
        });
        if (it == resolved.end()) return;
        const QString accessToken = it->accessToken;
        if (accessToken.isEmpty()) return;

        QStringList staleEmails;
        if (m_store)
            staleEmails = m_store->staleGooglePeopleEmails();

        for (const QString &sEmail : staleEmails) {
            if (m_destroying.load()) break;
            const QString url = resolveGooglePeopleAvatarUrl(sEmail, accessToken);
            if (url.isEmpty()) continue;
            const QString blob = fetchAvatarBlob(url, accessToken);
            if (!blob.startsWith("data:"_L1)) continue;
            const QString fileUrl = writeAvatarFile(sEmail, blob);
            if (fileUrl.isEmpty()) continue;
            if (m_store) m_store->updateContactAvatar(sEmail, fileUrl, "google-people"_L1);
        }
    });
}
