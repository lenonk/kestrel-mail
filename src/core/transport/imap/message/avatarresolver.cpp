#include "avatarresolver.h"
#include "../parser/responseparser.h"
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>

namespace Imap::AvatarResolver {

namespace {
    constexpr int kAvatarBlobTtlSeconds = 60 * 60;

    struct AvatarBlobCacheEntry {
        QByteArray bytes;
        QString mime;
        QDateTime fetchedAtUtc;
    };

    struct ImageFetch {
        QByteArray bytes;
        QString mime;
    };

    ImageFetch fetchImageBytes(const QUrl &url, int timeoutMs = 800) {
        QNetworkAccessManager nam;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kestrel-mail/1.0"));

        QNetworkReply *reply = nam.get(req);
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timeout.start(timeoutMs);
        loop.exec();

        ImageFetch result;
        if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
            const auto payload = reply->readAll();
            const auto ct = reply->header(QNetworkRequest::ContentTypeHeader).toString().trimmed().toLower();
            if (!payload.isEmpty() && payload.size() <= 512 * 1024 && ct.startsWith(QStringLiteral("image/"))) {
                result.bytes = payload;
                result.mime  = ct;
            }
        }

        if (!reply->isFinished()) reply->abort();
        reply->deleteLater();
        return result;
    }

    // Fetched once; used to detect Google's generic globe placeholder.
    const QByteArray &googleGlobePlaceholder() {
        static const QByteArray globe = fetchImageBytes(QUrl(QStringLiteral(
            "https://www.google.com/s2/favicons?domain=https://no-favicon-sentinel-kestrel.invalid&sz=128"))).bytes;
        return globe;
    }
}

QString
extractBimiLogoUrl(const QString &headerSource) {
    auto bimiLocation = Parser::extractField(headerSource, QStringLiteral("BIMI-Location")).trimmed();

    if (bimiLocation.startsWith('<') && bimiLocation.endsWith('>') && bimiLocation.size() > 2) {
        bimiLocation = bimiLocation.mid(1, bimiLocation.size() - 2).trimmed();
    }

    if (bimiLocation.startsWith(QStringLiteral("https://")) || bimiLocation.startsWith(QStringLiteral("http://"))) {
        return bimiLocation;
    }

    return {};
}

QString
senderDomainFromHeader(const QString &fromHeader) {
    const auto s = fromHeader.trimmed();
    static const QRegularExpression angleRe(QStringLiteral("<\\s*([^<>@\\s]+@[^<>@\\s]+)\\s*>"));

    const auto m1 = angleRe.match(s);
    QString email;
    if (m1.hasMatch()) {
        email = m1.captured(1).trimmed().toLower();
    }
    else {
        static const QRegularExpression plainRe(QStringLiteral("\\b([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
        if (const auto m2 = plainRe.match(s); m2.hasMatch())
            email = m2.captured(1).trimmed().toLower();
    }

    const auto at = email.indexOf('@');
    if (at < 0 || at + 1 >= email.size())
        return {};

    auto domain = email.mid(at + 1).trimmed();
    if (domain.startsWith('.'))
        domain = domain.mid(1);
    if (domain.endsWith('.'))
        domain.chop(1);
    if (domain.contains(QStringLiteral(".local")) || domain.contains(QStringLiteral(".invalid")))
        return {};

    const QStringList parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() <= 2)
        return domain;

    auto tail2 = parts.mid(parts.size() - 2).join('.');

    static const QSet cc2 = {
        QStringLiteral("co.uk"), QStringLiteral("org.uk"), QStringLiteral("gov.uk"), QStringLiteral("ac.uk"),
        QStringLiteral("com.au"), QStringLiteral("co.jp"), QStringLiteral("com.br"), QStringLiteral("com.mx")
    };

    if (cc2.contains(tail2) && parts.size() >= 3) {
        return parts.mid(parts.size() - 3).join('.');
    }

    return tail2;
}

QString
parseBimiLogoFromTxtRecord(const QString &txtRaw) {
    auto txt = txtRaw.trimmed();
    if (txt.startsWith('"') && txt.endsWith('"') && txt.size() >= 2) {
        txt = txt.mid(1, txt.size() - 2);
    }
    txt.replace(QStringLiteral("\""), QString());

    if (const auto lower = txt.toLower(); !lower.contains(QStringLiteral("v=bimi1")))
        return {};

    static const QRegularExpression logoRe(QStringLiteral("(?:^|;)\\s*l\\s*=\\s*([^;\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto m = logoRe.match(txt);
    if (!m.hasMatch())
        return {};

    auto url = m.captured(1).trimmed();
    if (url.startsWith(QStringLiteral("https://")) || url.startsWith(QStringLiteral("http://"))) {
        return url;
    }

    return {};
}

QString
resolveGooglePeopleAvatarUrl(const QString &senderEmail, const QString &accessToken) {
    static QMutex mutex;
    static QHash<QString, QString> cache;
    static bool peopleAuthUnavailable = false;

    const auto e = senderEmail.trimmed().toLower();
    if (e.isEmpty() || accessToken.trimmed().isEmpty())
        return {};

    {
        QMutexLocker lock(&mutex);
        if (cache.contains(e))
            return cache.value(e);
        if (peopleAuthUnavailable) {
            cache.insert(e, QString());
            return {};
        }
    }

    const auto at = e.indexOf('@');
    const auto local = (at > 0) ? e.left(at) : QString();
    const auto domain = (at > 0 && at + 1 < e.size()) ? e.mid(at + 1) : QString();

    static const QSet likelyPersonalDomains = {
        QStringLiteral("gmail.com"), QStringLiteral("googlemail.com"),
        QStringLiteral("outlook.com"), QStringLiteral("hotmail.com"), QStringLiteral("live.com"),
        QStringLiteral("icloud.com"), QStringLiteral("me.com"),
        QStringLiteral("yahoo.com"), QStringLiteral("yahoo.co.uk"), QStringLiteral("mail.com"),
    };

    if (!likelyPersonalDomains.contains(domain)) {
        QMutexLocker lock(&mutex);
        cache.insert(e, QString());
        return {};
    }

    // Skip automated mailbox names so they do not poison auth probing order.
    if (local.contains(QStringLiteral("noreply"))
            || local.contains(QStringLiteral("no-reply"))
            || local.contains(QStringLiteral("donotreply"))
            || local.contains(QStringLiteral("do-not-reply"))
            || local.contains(QStringLiteral("mailer-daemon"))
            || local.contains(QStringLiteral("postmaster"))) {
        QMutexLocker lock(&mutex);
        cache.insert(e, QString());
        return {};
    }

    QNetworkAccessManager nam;
    QUrl url(QStringLiteral("https://people.googleapis.com/v1/people:searchContacts"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"), e);
    q.addQueryItem(QStringLiteral("readMask"), QStringLiteral("photos,emailAddresses"));
    q.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(accessToken).toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kestrel-mail/1.0"));

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(350);
    loop.exec();

    QString found;
    auto outcome = QStringLiteral("none");
    if (!reply->isFinished()) {
        outcome = QStringLiteral("timeout");
    }
    else if (reply->error() == QNetworkReply::NoError) {
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        const auto results = obj.value(QStringLiteral("results")).toArray();
        if (!results.isEmpty()) {
            const auto person = results.at(0).toObject().value(QStringLiteral("person")).toObject();
            const auto photos = person.value(QStringLiteral("photos")).toArray();
            for (const auto &pv : photos) {
                const auto po = pv.toObject();
                const bool isDefault = po.value(QStringLiteral("default")).toBool(false);
                if (isDefault) continue;
                const auto u = po.value(QStringLiteral("url")).toString().trimmed();
                if (u.startsWith(QStringLiteral("https://")) || u.startsWith(QStringLiteral("http://"))) {
                    found = u;
                    outcome = QStringLiteral("hit");
                    break;
                }
            }
        }
    }
    else {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        outcome = QStringLiteral("http-%1").arg(status > 0 ? status : -1);
        if (status == 401 || status == 403) {
            QMutexLocker lock(&mutex);
            peopleAuthUnavailable = true;
        }
    }

    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();

    {
        QMutexLocker lock(&mutex);
        cache.insert(e, found);
    }

    return found;
}

QString
fetchAvatarBlob(const QString &url, const QString &bearerToken) {
    static QMutex mutex;
    static QHash<QString, AvatarBlobCacheEntry> cache;

    auto u = url.trimmed();
    if (!(u.startsWith(QStringLiteral("https://")) || u.startsWith(QStringLiteral("http://")))) {
        return u;
    }

    const auto now = QDateTime::currentDateTimeUtc();
    {
        QMutexLocker lock(&mutex);
        if (const auto it = cache.constFind(u); it != cache.constEnd() && it.value().fetchedAtUtc.secsTo(now) < kAvatarBlobTtlSeconds) {
            const auto mime = it.value().mime.isEmpty() ? QStringLiteral("image/png") : it.value().mime;
            return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(it.value().bytes.toBase64()));
        }
    }

    QNetworkAccessManager nam;
    QNetworkRequest req { QUrl(u) };
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kestrel-mail/1.0"));
    if (!bearerToken.isEmpty())
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(bearerToken).toUtf8());

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(500);
    loop.exec();

    auto out = u;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const auto payload = reply->readAll();
        const auto contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().trimmed().toLower();
        if (!payload.isEmpty() && payload.size() <= 512 * 1024 && contentType.startsWith(QStringLiteral("image/"))) {
            AvatarBlobCacheEntry entry;
            entry.bytes = payload;
            entry.mime = contentType;
            entry.fetchedAtUtc = now;
            {
                QMutexLocker lock(&mutex);
                cache.insert(u, entry);
            }
            out = QStringLiteral("data:%1;base64,%2").arg(contentType, QString::fromLatin1(payload.toBase64()));
        }
    }

    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();

    return out;
}

QString
extractListIdDomain(const QString &headerSource) {
    QString listId = Parser::extractField(headerSource, QStringLiteral("List-ID")).trimmed();
    if (listId.isEmpty())
        return {};

    // Typical forms:
    // - <news.example.com>
    // - "Brand News" <news.example.com>
    // - news.example.com
    if (listId.startsWith('<') && listId.endsWith('>') && listId.size() > 2) {
        listId = listId.mid(1, listId.size() - 2).trimmed();
    }

    const auto lt = listId.indexOf('<');
    if (const auto gt = listId.indexOf('>', lt + 1); lt >= 0 && gt > lt) {
        listId = listId.mid(lt + 1, gt - lt - 1).trimmed();
    }

    // Remove the display-name cruft and keep the token with dots.
    listId = listId.toLower().trimmed();
    listId.remove('"');
    static const QRegularExpression domainRe(QStringLiteral("([a-z0-9.-]+\\.[a-z]{2,})"));
    const auto m = domainRe.match(listId);
    if (!m.hasMatch())
        return {};

    QString domain = m.captured(1).trimmed();
    if (domain.startsWith('.'))
        domain = domain.mid(1);

    if (domain.endsWith('.'))
        domain.chop(1);

    if (domain.contains(QStringLiteral(".local")) || domain.contains(QStringLiteral(".invalid")))
        return {};

    // Normalize to registrable-ish base domain for favicon lookup.
    const auto parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() <= 2)
        return domain;

    auto tail2 = parts.mid(parts.size() - 2).join('.');

    static const QSet<QString> cc2 = {
        QStringLiteral("co.uk"), QStringLiteral("org.uk"), QStringLiteral("gov.uk"), QStringLiteral("ac.uk"),
        QStringLiteral("com.au"), QStringLiteral("co.jp"), QStringLiteral("com.br"), QStringLiteral("com.mx")
    };

    if (cc2.contains(tail2) && parts.size() >= 3) {
        return parts.mid(parts.size() - 3).join('.');
    }

    return tail2;
}

QString
resolveBimiLogoUrlViaDoh(const QString& domain) {
    static QMutex mutex;
    static QHash<QString, QString> cache;

    const QString d = domain.trimmed().toLower();
    if (d.isEmpty())
        return {};

    static const QSet bimiSkip = {
        QStringLiteral("gmail.com"), QStringLiteral("google.com"), QStringLiteral("googlemail.com"),
        QStringLiteral("twitter.com"), QStringLiteral("youtube.com"), QStringLiteral("icloud.com"), QStringLiteral("outlook.com"),
        QStringLiteral("mail.com"),
    };

    if (bimiSkip.contains(d)) {
        QMutexLocker lock(&mutex);
        cache.insert(d, QString());
        return {};
    }

    {
        QMutexLocker lock(&mutex);
        if (cache.contains(d))
            return cache.value(d);
    }

    const QUrl url(QStringLiteral("https://dns.google/resolve?name=default._bimi.%1&type=TXT").arg(d));
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kestrel-mail/1.0"));

    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(1200);
    loop.exec();

    QString found;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        for (const auto answers = obj.value(QStringLiteral("Answer")).toArray(); const auto &av : answers) {
            const auto ao = av.toObject();
            if (const auto type = ao.value(QStringLiteral("type")).toInt(0); type != 16)
                continue; // TXT record
            const auto data = ao.value(QStringLiteral("data")).toString().trimmed();
            if (const auto logo = parseBimiLogoFromTxtRecord(data); !logo.isEmpty()) {
                found = logo;
                break;
            }
        }
    }

    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();

    {
        QMutexLocker lock(&mutex);
        cache.insert(d, found);
    }

    return found;
}

QString
resolveGravatarUrl(const QString &email) {
    static QMutex mutex;
    static QHash<QString, QString> cache;

    const QString e = email.trimmed().toLower();
    if (e.isEmpty())
        return {};

    {
        QMutexLocker lock(&mutex);
        if (cache.contains(e))
            return cache.value(e);
    }

    const auto at = e.indexOf('@');
    const auto local = (at > 0) ? e.left(at) : QString();
    if (local.contains(QStringLiteral("noreply"))
            || local.contains(QStringLiteral("no-reply"))
            || local.contains(QStringLiteral("donotreply"))
            || local.contains(QStringLiteral("do-not-reply"))
            || local.contains(QStringLiteral("mailer-daemon"))
            || local.contains(QStringLiteral("postmaster"))) {
        QMutexLocker lock(&mutex);
        cache.insert(e, {});
        return {};
    }

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(e.toUtf8(), QCryptographicHash::Md5).toHex());
    const auto result = fetchImageBytes(
        QUrl(QStringLiteral("https://www.gravatar.com/avatar/%1?s=128&d=404").arg(hash)));

    const QString found = result.bytes.isEmpty()
        ? QString{}
        : QStringLiteral("data:%1;base64,%2").arg(result.mime, QString::fromLatin1(result.bytes.toBase64()));

    {
        QMutexLocker lock(&mutex);
        cache.insert(e, found);
    }
    return found;
}

/*QString
resolveFaviconLogoUrl(const QString &domain) {
    static QHash<QString, QString> cache;

    const QString d0 = domain.trimmed().toLower();
    if (d0.isEmpty())
        return {};

    if (cache.contains(d0))
        return cache.value(d0);

    // Personal/consumer domains: use initials, not a brand logo.
    static const QSet<QString> skip = {
        QStringLiteral("gmail.com"), QStringLiteral("googlemail.com"),
        QStringLiteral("outlook.com"), QStringLiteral("hotmail.com"), QStringLiteral("live.com"),
        QStringLiteral("icloud.com"), QStringLiteral("me.com"),
        QStringLiteral("yahoo.com"), QStringLiteral("yahoo.co.uk"),
        QStringLiteral("google.com"), QStringLiteral("twitter.com"), QStringLiteral("youtube.com")
    };

    if (skip.contains(d0)) {
        cache.insert(d0, {});
        return {};
    }

    auto toDataUri = [](const ImageFetch &f) -> QString {
        return QStringLiteral("data:%1;base64,%2").arg(f.mime, QString::fromLatin1(f.bytes.toBase64()));
    };

    auto tryUrl = [&](const QUrl &u) -> QString {
        const auto r = fetchImageBytes(u);
        if (r.bytes.isEmpty())
            return {};
        return toDataUri(r);
    };

    auto tryHost = [&](const QString &host) -> QString {
        // Try a small set of common icon locations (largest/nicest first).
        // Note: apple-touch-icon is frequently 180x180+ and looks better in UIs.
        static const QStringList paths = {
            QStringLiteral("/apple-touch-icon.png"),
            QStringLiteral("/apple-touch-icon-precomposed.png"),
            QStringLiteral("/favicon.svg"),
            QStringLiteral("/favicon.png"),
            QStringLiteral("/favicon.ico"),
        };

        for (const QString &scheme : { QStringLiteral("https"), QStringLiteral("http") }) {
            for (const QString &path : paths) {
                const QUrl u(QStringLiteral("%1://%2%3").arg(scheme, host, path));
                const auto dataUri = tryUrl(u);
                if (!dataUri.isEmpty())
                    return dataUri;
            }
        }
        return {};
    };

    QString found;

    // 1) Prefer origin-known icon URLs (Chromium-ish: declared first, default path next;
    //    we approximate "declared first" by trying common high-quality endpoints).
    found = tryHost(d0);

    // Try "www." variant if needed.
    if (found.isEmpty() && !d0.startsWith(QStringLiteral("www.")))
        found = tryHost(QStringLiteral("www.%1").arg(d0));

    // 2) Fall back to Google favicon service (your existing behavior + placeholder filter).
    if (found.isEmpty()) {
        const auto googleResult = fetchImageBytes(QUrl(
            QStringLiteral("https://www.google.com/s2/favicons?domain=%1&sz=128").arg(d0)));
        if (!googleResult.bytes.isEmpty() && googleResult.bytes != googleGlobePlaceholder())
            found = toDataUri(googleResult);
    }

    // 3) Fall back to DuckDuckGo.
    if (found.isEmpty()) {
        const auto ddgResult = fetchImageBytes(
            QUrl(QStringLiteral("https://icons.duckduckgo.com/ip3/%1.ico").arg(d0)));
        if (!ddgResult.bytes.isEmpty())
            found = toDataUri(ddgResult);
    }

    cache.insert(d0, found);
    return found;
}*/

QString
resolveFaviconLogoUrl(const QString &domain) {
    static QMutex mutex;
    static QHash<QString, QString> cache;

    const QString d = domain.trimmed().toLower();
    if (d.isEmpty())
        return {};

    {
        QMutexLocker lock(&mutex);
        if (cache.contains(d))
            return cache.value(d);
    }

    // Personal/consumer domains: use initials, not a brand logo.
    static const QSet<QString> skip = {
        QStringLiteral("gmail.com"), QStringLiteral("googlemail.com"),
        QStringLiteral("outlook.com"), QStringLiteral("hotmail.com"), QStringLiteral("live.com"),
        QStringLiteral("icloud.com"), QStringLiteral("me.com"),
        QStringLiteral("yahoo.com"), QStringLiteral("yahoo.co.uk"),
        QStringLiteral("google.com"), QStringLiteral("twitter.com"), QStringLiteral("youtube.com"),
        QStringLiteral("mail.com")
    };

    if (skip.contains(d)) {
        QMutexLocker lock(&mutex);
        cache.insert(d, {});
        return {};
    }

    auto toDataUri = [](const ImageFetch &f) -> QString {
        return QStringLiteral("data:%1;base64,%2").arg(f.mime, QString::fromLatin1(f.bytes.toBase64()));
    };

    QString found;

    const auto googleResult = fetchImageBytes(QUrl(
        QStringLiteral("https://www.google.com/s2/favicons?domain=%1&sz=128").arg(d)));
    if (!googleResult.bytes.isEmpty() && googleResult.bytes != googleGlobePlaceholder())
        found = toDataUri(googleResult);

    if (found.isEmpty()) {
        const auto ddgResult = fetchImageBytes(QUrl(QStringLiteral("https://icons.duckduckgo.com/ip3/%1.ico").arg(d)));
        if (!ddgResult.bytes.isEmpty())
            found = toDataUri(ddgResult);
    }

    {
        QMutexLocker lock(&mutex);
        cache.insert(d, found);
    }

    return found;
}

} // namespace Imap::AvatarResolver

