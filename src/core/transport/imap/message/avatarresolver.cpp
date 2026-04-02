#include "avatarresolver.h"

#include <qcache.h>

#include "../../../utils.h"
#include "../parser/responseparser.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

using namespace Qt::Literals::StringLiterals;

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
        QString    mime;
        int        width  = 0;
        int        height = 0;
    };

    // One QNetworkAccessManager per thread. Avoids rebuilding the SSL context,
    // proxy configuration, and connection cache on every individual fetch call.
    QNetworkAccessManager &sharedNam() {
        thread_local static QNetworkAccessManager nam;
        return nam;
    }

    ImageFetch fetchImageBytes(const QUrl &url, int timeoutMs = 800) {
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, "kestrel-mail/1.0"_L1);

        QNetworkReply *reply = sharedNam().get(req);
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
            if (!payload.isEmpty() && payload.size() <= 512 * 1024 && ct.startsWith("image/"_L1)) {
                QImage img;
                if (img.loadFromData(payload)) {
                    result.bytes  = payload;
                    result.mime   = ct;
                    result.width  = img.width();
                    result.height = img.height();
                }
            }
        }

        if (!reply->isFinished()) reply->abort();
        reply->deleteLater();
        return result;
    }

    // Fetched once per thread; used to detect Google's generic globe placeholder.
    const QByteArray &googleGlobePlaceholder() {
        static const QByteArray globe = fetchImageBytes(QUrl(
            "https://www.google.com/s2/favicons?domain=https://no-favicon-sentinel-kestrel.invalid&sz=128"_L1)).bytes;
        return globe;
    }

} // namespace

QString
extractBimiLogoUrl(const QString &headerSource) {
    auto bimiLocation = Parser::extractField(headerSource, "BIMI-Location"_L1).trimmed();

    if (bimiLocation.startsWith('<') && bimiLocation.endsWith('>') && bimiLocation.size() > 2) {
        bimiLocation = bimiLocation.mid(1, bimiLocation.size() - 2).trimmed();
    }

    if (bimiLocation.startsWith("https://"_L1) || bimiLocation.startsWith("http://"_L1)) {
        return bimiLocation;
    }

    return {};
}

QString
senderDomainFromHeader(const QString &fromHeader) {
    const auto s = fromHeader.trimmed();
    static const QRegularExpression angleRe("<\\s*([^<>@\\s]+@[^<>@\\s]+)\\s*>"_L1);

    const auto m1 = angleRe.match(s);
    QString email;
    if (m1.hasMatch()) {
        email = Kestrel::normalizeEmail(m1.captured(1));
    }
    else {
        static const QRegularExpression plainRe("\\b([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})\\b"_L1,
                                         QRegularExpression::CaseInsensitiveOption);
        if (const auto m2 = plainRe.match(s); m2.hasMatch())
            email = Kestrel::normalizeEmail(m2.captured(1));
    }

    const auto at = email.indexOf('@');
    if (at < 0 || at + 1 >= email.size())
        return {};

    auto domain = email.mid(at + 1).trimmed();
    if (domain.startsWith('.'))
        domain = domain.mid(1);
    if (domain.endsWith('.'))
        domain.chop(1);
    if (domain.contains(".local"_L1) || domain.contains(".invalid"_L1))
        return {};

    const QStringList parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() <= 2)
        return domain;

    auto tail2 = parts.mid(parts.size() - 2).join('.');

    static const QSet<QString> cc2 = {
        "co.uk"_L1, "org.uk"_L1, "gov.uk"_L1, "ac.uk"_L1,
        "com.au"_L1, "co.jp"_L1, "com.br"_L1, "com.mx"_L1
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
    txt.replace("\""_L1, QString());

    if (const auto lower = txt.toLower(); !lower.contains("v=bimi1"_L1))
        return {};

    static const QRegularExpression logoRe("(?:^|;)\\s*l\\s*=\\s*([^;\\s]+)"_L1,
        QRegularExpression::CaseInsensitiveOption);

    const auto m = logoRe.match(txt);
    if (!m.hasMatch())
        return {};

    auto url = m.captured(1).trimmed();
    if (url.startsWith("https://"_L1) || url.startsWith("http://"_L1)) {
        return url;
    }

    return {};
}

QString
resolveGooglePeopleAvatarUrl(const QString &senderEmail, const QString &accessToken) {
    static QMutex mutex;
    static QHash<QString, QString> cache;
    static bool peopleAuthUnavailable = false;

    const auto e = Kestrel::normalizeEmail(senderEmail);
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

    static const QSet<QString> likelyPersonalDomains = {
        "gmail.com"_L1, "googlemail.com"_L1,
        "outlook.com"_L1, "hotmail.com"_L1, "live.com"_L1,
        "icloud.com"_L1, "me.com"_L1,
        "yahoo.com"_L1, "yahoo.co.uk"_L1, "mail.com"_L1,
    };

    if (!likelyPersonalDomains.contains(domain)) {
        QMutexLocker lock(&mutex);
        cache.insert(e, QString());
        return {};
    }

    // Skip automated mailbox names so they do not poison auth probing order.
    if (local.contains("noreply"_L1)
            || local.contains("no-reply"_L1)
            || local.contains("donotreply"_L1)
            || local.contains("do-not-reply"_L1)
            || local.contains("mailer-daemon"_L1)
            || local.contains("postmaster"_L1)) {
        QMutexLocker lock(&mutex);
        cache.insert(e, QString());
        return {};
    }

    QUrl url("https://people.googleapis.com/v1/people:searchContacts"_L1);
    QUrlQuery q;
    q.addQueryItem("query"_L1, e);
    q.addQueryItem("readMask"_L1, "photos,emailAddresses"_L1);
    q.addQueryItem("pageSize"_L1, "1"_L1);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer %1"_L1.arg(accessToken).toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader, "kestrel-mail/1.0"_L1);

    QNetworkReply *reply = sharedNam().get(req);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(350);
    loop.exec();

    QString found;
    QString outcome = "none"_L1;
    if (!reply->isFinished()) {
        outcome = "timeout"_L1;
    }
    else if (reply->error() == QNetworkReply::NoError) {
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        const auto results = obj.value("results"_L1).toArray();
        if (!results.isEmpty()) {
            const auto person = results.at(0).toObject().value("person"_L1).toObject();
            const auto photos = person.value("photos"_L1).toArray();
            for (const auto &pv : photos) {
                const auto po = pv.toObject();
                const bool isDefault = po.value("default"_L1).toBool(false);
                if (isDefault) continue;
                const auto u = po.value("url"_L1).toString().trimmed();
                if (u.startsWith("https://"_L1) || u.startsWith("http://"_L1)) {
                    found = u;
                    outcome = "hit"_L1;
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
    if (!(u.startsWith("https://"_L1) || u.startsWith("http://"_L1))) {
        return u;
    }

    const auto now = QDateTime::currentDateTimeUtc();
    {
        QMutexLocker lock(&mutex);
        if (const auto it = cache.constFind(u); it != cache.constEnd() && it.value().fetchedAtUtc.secsTo(now) < kAvatarBlobTtlSeconds) {
            const auto mime = it.value().mime.isEmpty() ? "image/png"_L1 : it.value().mime;
            return "data:%1;base64,%2"_L1.arg(mime, QString::fromLatin1(it.value().bytes.toBase64()));
        }
    }

    QNetworkRequest req { QUrl(u) };
    req.setHeader(QNetworkRequest::UserAgentHeader, "kestrel-mail/1.0"_L1);
    if (!bearerToken.isEmpty())
        req.setRawHeader("Authorization", "Bearer %1"_L1.arg(bearerToken).toUtf8());

    QNetworkReply *reply = sharedNam().get(req);
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
        if (!payload.isEmpty() && payload.size() <= 512 * 1024 && contentType.startsWith("image/"_L1)) {
            AvatarBlobCacheEntry entry;
            entry.bytes = payload;
            entry.mime = contentType;
            entry.fetchedAtUtc = now;
            {
                QMutexLocker lock(&mutex);
                cache.insert(u, entry);
            }
            out = "data:%1;base64,%2"_L1.arg(contentType, QString::fromLatin1(payload.toBase64()));
        }
    }

    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();

    return out;
}

QString
extractListIdDomain(const QString &headerSource) {
    QString listId = Parser::extractField(headerSource, "List-ID"_L1).trimmed();
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
    static const QRegularExpression domainRe("([a-z0-9.-]+\\.[a-z]{2,})"_L1);
    const auto m = domainRe.match(listId);
    if (!m.hasMatch())
        return {};

    QString domain = m.captured(1).trimmed();
    if (domain.startsWith('.'))
        domain = domain.mid(1);

    if (domain.endsWith('.'))
        domain.chop(1);

    if (domain.contains(".local"_L1) || domain.contains(".invalid"_L1))
        return {};

    // Normalize to registrable-ish base domain for favicon lookup.
    const auto parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() <= 2)
        return domain;

    auto tail2 = parts.mid(parts.size() - 2).join('.');

    static const QSet<QString> cc2 = {
        "co.uk"_L1, "org.uk"_L1, "gov.uk"_L1, "ac.uk"_L1,
        "com.au"_L1, "co.jp"_L1, "com.br"_L1, "com.mx"_L1
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

    static const QSet<QString> bimiSkip = {
        "gmail.com"_L1, "google.com"_L1, "googlemail.com"_L1,
        "icloud.com"_L1, "outlook.com"_L1,
        "mail.com"_L1,
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

    const QUrl url("https://dns.google/resolve?name=default._bimi.%1&type=TXT"_L1.arg(d));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "kestrel-mail/1.0"_L1);

    QNetworkReply *reply = sharedNam().get(req);
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
        for (const auto answers = obj.value("Answer"_L1).toArray(); const auto &av : answers) {
            const auto ao = av.toObject();
            if (const auto type = ao.value("type"_L1).toInt(0); type != 16)
                continue; // TXT record
            const auto data = ao.value("data"_L1).toString().trimmed();
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

    const QString e = Kestrel::normalizeEmail(email);
    if (e.isEmpty())
        return {};

    {
        QMutexLocker lock(&mutex);
        if (cache.contains(e))
            return cache.value(e);
    }

    const auto at = e.indexOf('@');
    const auto local = (at > 0) ? e.left(at) : QString();
    if (local.contains("noreply"_L1)
            || local.contains("no-reply"_L1)
            || local.contains("donotreply"_L1)
            || local.contains("do-not-reply"_L1)
            || local.contains("mailer-daemon"_L1)
            || local.contains("postmaster"_L1)) {
        QMutexLocker lock(&mutex);
        cache.insert(e, {});
        return {};
    }

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(e.toUtf8(), QCryptographicHash::Md5).toHex());
    const auto result = fetchImageBytes(
        QUrl("https://www.gravatar.com/avatar/%1?s=128&d=404"_L1.arg(hash)));

    const QString found = result.bytes.isEmpty()
        ? QString{}
        : "data:%1;base64,%2"_L1.arg(result.mime, QString::fromLatin1(result.bytes.toBase64()));

    {
        QMutexLocker lock(&mutex);
        cache.insert(e, found);
    }
    return found;
}

// Version 2 -
// QString
// resolveFaviconLogoUrl(const QString &domain) {
//     static QCache<QString, QString> cache(/*maxCost*/ 2048);
//     const QString d0 = domain.trimmed().toLower();
//     if (d0.isEmpty())
//         return {};
//
//     if (auto *hit = cache.object(d0))
//         return *hit;
//
//     static const QSet<QString> skip = {
//         QStringLiteral("gmail.com"), QStringLiteral("googlemail.com"),
//         QStringLiteral("outlook.com"), QStringLiteral("hotmail.com"), QStringLiteral("live.com"),
//         QStringLiteral("icloud.com"), QStringLiteral("me.com"),
//         QStringLiteral("yahoo.com"), QStringLiteral("yahoo.co.uk"),
//         QStringLiteral("google.com"), QStringLiteral("twitter.com"), QStringLiteral("youtube.com"),
//         QStringLiteral("mail.com"),
//     };
//
//     if (skip.contains(d0)) {
//         cache.insert(d0, new QString(), 1);
//         return {};
//     }
//
//     auto toDataUri = [](const ImageFetch &f) -> QString {
//         return QStringLiteral("data:%1;base64,%2")
//             .arg(f.mime, QString::fromLatin1(f.bytes.toBase64()));
//     };
//
//     auto isUsableImage = [](const ImageFetch &f) -> bool {
//         if (f.bytes.isEmpty()) return false;
//         // If you can, decode it to confirm it's a real image.
//         QImage img;
//         img.loadFromData(f.bytes);
//         return !img.isNull() && img.width() > 0 && img.height() > 0;
//     };
//
//     auto fetchUrl = [&](const QUrl &u) -> ImageFetch {
//         // Your existing fetch. Consider setting a UA + short timeout.
//         return fetchImageBytes(u);
//     };
//
//     QString found;
//
//     // ---- A) Origin HTML discovery ----
//     // If you don't already have an HTML fetcher, add one.
//     // Pseudocode: fetchHtml(QUrl) -> QString
//     auto tryOrigin = [&](const QString &scheme) -> bool {
//         const QUrl base(QStringLiteral("%1://%2/").arg(scheme, d0));
//
//         const QString html = fetchHtml(base); // implement
//         if (html.isEmpty())
//             return false;
//
//         // Parse <link ...> candidates from <head>
//         // Use QXmlStreamReader only if you sanitize; HTML isn't XML.
//         // Practical: cheap regex for rel/icon + href, then resolve URLs.
//         const QList<QUrl> candidates = extractIconCandidates(html, base); // implement
//
//         // Fetch a few candidates and pick best.
//         ImageFetch best;
//         int bestScore = -1;
//
//         for (const QUrl &iconUrl : candidates.mid(0, 6)) {
//             const auto f = fetchUrl(iconUrl);
//             if (!isUsableImage(f))
//                 continue;
//
//             const int score = scoreIcon(iconUrl, f, /*hintSizesFromLink*/{}); // implement
//             if (score > bestScore) {
//                 bestScore = score;
//                 best = f;
//             }
//         }
//
//         if (bestScore >= 0) {
//             found = toDataUri(best);
//             return true;
//         }
//
//         return false;
//     };
//
//     // Try HTTPS first.
//     if (found.isEmpty())
//         tryOrigin(QStringLiteral("https"));
//     if (found.isEmpty())
//         tryOrigin(QStringLiteral("http"));
//
//     // ---- B) /favicon.ico fallback ----
//     if (found.isEmpty()) {
//         for (const QString &scheme : {QStringLiteral("https"), QStringLiteral("http")}) {
//             const QUrl icoUrl(QStringLiteral("%1://%2/favicon.ico").arg(scheme, d0));
//             const auto f = fetchUrl(icoUrl);
//             if (isUsableImage(f)) {
//                 found = toDataUri(f);
//                 break;
//             }
//         }
//     }
//
//     // ---- C) External services fallback (your current approach) ----
//     if (found.isEmpty()) {
//         const auto googleResult = fetchUrl(QUrl(
//             QStringLiteral("https://www.google.com/s2/favicons?domain=%1&sz=128").arg(d0)));
//
//         if (isUsableImage(googleResult) && googleResult.bytes != googleGlobePlaceholder())
//             found = toDataUri(googleResult);
//     }
//
//     if (found.isEmpty()) {
//         const auto ddgResult = fetchUrl(QUrl(QStringLiteral("https://icons.duckduckgo.com/ip3/%1.ico").arg(d0)));
//         if (isUsableImage(ddgResult))
//             found = toDataUri(ddgResult);
//     }
//
//     cache.insert(d0, new QString(found), 1);
//
//     qInfo().noquote() << "[avatar-favicon]"
//                       << "domain=" << d0
//                       << "result=" << (!found.isEmpty() ? "hit" : "miss");
//
//     return found;
// }

// Version 1 - Best overall resuls so far
QString resolveFaviconLogoUrl(const QString &domain) {
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
        "gmail.com"_L1, "googlemail.com"_L1,
        "outlook.com"_L1, "hotmail.com"_L1, "live.com"_L1,
        "icloud.com"_L1, "me.com"_L1,
        "yahoo.com"_L1, "yahoo.co.uk"_L1,
        "google.com"_L1,
        "mail.com"_L1
    };

    if (skip.contains(d)) {
        QMutexLocker lock(&mutex);
        cache.insert(d, {});
        return {};
    }

    auto toDataUri = [](const ImageFetch &f) -> QString {
        return "data:%1;base64,%2"_L1.arg(f.mime, QString::fromLatin1(f.bytes.toBase64()));
    };

    QString found;

    const auto googleResult = fetchImageBytes(QUrl(
        "https://www.google.com/s2/favicons?domain=%1&sz=128"_L1.arg(d)));
    if (!googleResult.bytes.isEmpty() && googleResult.bytes != googleGlobePlaceholder())
        found = toDataUri(googleResult);

    if (found.isEmpty()) {
        const auto ddgResult = fetchImageBytes(QUrl("https://icons.duckduckgo.com/ip3/%1.ico"_L1.arg(d)));
        if (!ddgResult.bytes.isEmpty())
            found = toDataUri(ddgResult);
    }

    {
        QMutexLocker lock(&mutex);
        cache.insert(d, found);
    }

    return found;
}

// Version 3 - Slow, and I don't like the results.  Low quality icons
// QString resolveFaviconLogoUrl(const QString &domain) {
//     static QMutex mutex;
//     static QHash<QString, QString> cache;
//
//     const QString d = domain.trimmed().toLower();
//     if (d.isEmpty())
//         return {};
//
//     {
//         QMutexLocker lock(&mutex);
//         if (cache.contains(d))
//             return cache.value(d);
//     }
//
//     static const QSet<QString> skip = {
//         u"gmail.com"_s,      u"googlemail.com"_s,
//         u"outlook.com"_s,    u"hotmail.com"_s,    u"live.com"_s,
//         u"icloud.com"_s,     u"me.com"_s,
//         u"yahoo.com"_s,      u"yahoo.co.uk"_s,
//         u"google.com"_s,     u"twitter.com"_s,    u"youtube.com"_s,
//         u"mail.com"_s,
//     };
//
//     if (skip.contains(d)) {
//         QMutexLocker lock(&mutex);
//         cache.insert(d, {});
//         return {};
//     }
//
//     auto toDataUri = [](const ImageFetch &f) -> QString {
//         return QStringLiteral("data:%1;base64,%2")
//             .arg(f.mime, QString::fromLatin1(f.bytes.toBase64()));
//     };
//     auto isUsable = [](const ImageFetch &f) {
//         return !f.bytes.isEmpty() && f.width > 0 && f.height > 0;
//     };
//
//     QString found;
//
//     // ---- A) Origin HTML discovery ----
//     // Fetch the site root, parse declared <link rel="icon"> candidates, score and
//     // pick the best. Try the bare domain first, then www. if needed, https before http.
//     auto tryOrigin = [&](const QString &host) -> bool {
//         for (const QString &scheme : { u"https"_s, u"http"_s }) {
//             const QUrl base(QStringLiteral("%1://%2/").arg(scheme, host));
//             const QString html = fetchHtml(base);
//             if (html.isEmpty())
//                 continue;
//
//             const QList<IconCandidate> candidates = extractIconCandidates(html, base);
//             ImageFetch best;
//             int bestScore = -1;
//
//             for (const auto &c : candidates.mid(0, 6)) {
//                 const auto f = fetchImageBytes(c.url);
//                 if (!isUsable(f))
//                     continue;
//                 const int score = scoreIcon(c.url, f, c.sizes);
//                 if (score > bestScore) { bestScore = score; best = f; }
//             }
//
//             if (bestScore >= 0) {
//                 found = toDataUri(best);
//                 return true;
//             }
//         }
//         return false;
//     };
//
//     if (!tryOrigin(d) && !d.startsWith("www."_L1))
//         tryOrigin(u"www."_s + d);
//
//     // ---- B) /favicon.ico fallback ----
//     if (found.isEmpty()) {
//         for (const QString &scheme : { u"https"_s, u"http"_s }) {
//             const auto f = fetchImageBytes(
//                 QUrl(QStringLiteral("%1://%2/favicon.ico").arg(scheme, d)));
//             if (isUsable(f)) { found = toDataUri(f); break; }
//         }
//     }
//
//     // ---- C) External service fallbacks ----
//     if (found.isEmpty()) {
//         const auto f = fetchImageBytes(QUrl(
//             QStringLiteral("https://www.google.com/s2/favicons?domain=%1&sz=128").arg(d)));
//         if (isUsable(f) && f.bytes != googleGlobePlaceholder())
//             found = toDataUri(f);
//     }
//
//     if (found.isEmpty()) {
//         const auto f = fetchImageBytes(
//             QUrl(QStringLiteral("https://icons.duckduckgo.com/ip3/%1.ico").arg(d)));
//         if (isUsable(f))
//             found = toDataUri(f);
//     }
//
//     qInfo().noquote() << "[avatar-favicon]"
//                       << "domain=" << d
//                       << "result=" << (!found.isEmpty() ? "hit" : "miss");
//
//     {
//         QMutexLocker lock(&mutex);
//         cache.insert(d, found);
//     }
//     return found;
// }

QString
writeAvatarFile(const QString &email, const QString &dataUri)
{
    static const QRegularExpression re(R"(^data:(image/[^;,]+);base64,(.+)$)"_L1,
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(dataUri.trimmed());
    if (!m.hasMatch())
        return {};

    const QString mime = m.captured(1).trimmed().toLower();
    const QByteArray bytes = QByteArray::fromBase64(m.captured(2).trimmed().toLatin1());
    if (bytes.isEmpty())
        return {};

    QString ext = "bin"_L1;
    if (mime.startsWith("image/png"_L1))         ext = "png"_L1;
    else if (mime.contains("jpeg"_L1) || mime.contains("jpg"_L1)) ext = "jpg"_L1;
    else if (mime.startsWith("image/webp"_L1))   ext = "webp"_L1;
    else if (mime.startsWith("image/gif"_L1))    ext = "gif"_L1;
    else if (mime.startsWith("image/svg"_L1))    ext = "svg"_L1;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + "/kestrel-mail/avatars"_L1;
    QDir().mkpath(dir);

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(Kestrel::normalizeEmail(email).toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString absPath = dir + "/"_L1 + hash + "."_L1 + ext;

    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    f.close();

    return "file://"_L1 + absPath;
}

} // namespace Imap::AvatarResolver
