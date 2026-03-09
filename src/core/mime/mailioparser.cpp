#include "mailioparser.h"

#include <QRegularExpression>
#include <QHash>
#include <QDebug>
#include <functional>

#include <mailio/message.hpp>
#include <mailio/mime.hpp>

namespace Mime {

namespace {

// Mailio enforces RFC 2822's ~998-byte per-line limit and throws
// "Line policy overflow in a header" for any physical line that is too long.
// This can occur in top-level headers (DKIM-Signature, ARC-*, etc.) as well
// as in nested MIME part headers (Content-Type with a long filename=, etc.).
// We don't need any of those headers for body extraction, so we hard-truncate
// every physical line in the entire literal to 998 bytes before parsing.
// Base64 and quoted-printable body lines are always well under this limit,
// so no body content is lost.
static QByteArray truncateLongHeaderLines(const QByteArray &rfc822)
{
    QByteArray out;
    out.reserve(rfc822.size());

    int i = 0;
    while (i < rfc822.size()) {
        const int end = rfc822.indexOf("\r\n", i);
        if (end < 0) {
            out += rfc822.mid(i, qMin(rfc822.size() - i, 998));
            break;
        }
        out += rfc822.mid(i, qMin(end - i, 998));
        out += "\r\n";
        i = end + 2;
    }

    return out;
}

QByteArray extractRfc822Literal(const QByteArray &fetchRespRaw)
{
    int i = 0;
    while (i < fetchRespRaw.size()) {
        if (fetchRespRaw[i] != '{') {
            ++i;
            continue;
        }

        int j = i + 1;
        while (j < fetchRespRaw.size() && fetchRespRaw[j] >= '0' && fetchRespRaw[j] <= '9') ++j;
        if (j <= i + 1 || j + 2 >= fetchRespRaw.size() || fetchRespRaw[j] != '}'
            || fetchRespRaw[j + 1] != '\r' || fetchRespRaw[j + 2] != '\n') {
            ++i;
            continue;
        }

        bool ok = false;
        const int literalLen = fetchRespRaw.mid(i + 1, j - (i + 1)).toInt(&ok);
        if (!ok || literalLen <= 0) {
            ++i;
            continue;
        }

        const int literalStart = j + 3;
        const int literalEndExclusive = qMin(fetchRespRaw.size(), literalStart + literalLen);
        if (literalEndExclusive <= literalStart) {
            i = j + 1;
            continue;
        }

        return fetchRespRaw.mid(literalStart, literalEndExclusive - literalStart);
    }
    return {};
}

QString normalizeCid(QString cid)
{
    cid = cid.trimmed();
    if (cid.startsWith(QStringLiteral("cid:"), Qt::CaseInsensitive)) {
        cid = cid.mid(4).trimmed();
    }
    if (cid.startsWith('<') && cid.endsWith('>') && cid.size() > 2) {
        cid = cid.mid(1, cid.size() - 2).trimmed();
    }
    return cid.toLower();
}

void collectMimeParts(const mailio::mime &part, QString &htmlOut, QString &plainOut, QHash<QString, QString> &cidMap)
{
    auto &mutablePart = const_cast<mailio::mime&>(part);
    const auto ct = mutablePart.content_type();
    const auto parts = part.parts();

    for (const auto &child : parts) {
        collectMimeParts(child, htmlOut, plainOut, cidMap);
    }

    const std::string subtype = ct.media_subtype();
    const auto mediaType = ct.media_type();

    if (htmlOut.isEmpty() && mediaType == mailio::mime::media_type_t::TEXT && subtype == "html") {
        const std::string body = part.content();
        const QString html = QString::fromUtf8(body.c_str(), static_cast<int>(body.size())).trimmed();
        if (!html.isEmpty()) {
            htmlOut = html;
        }
    }

    if (plainOut.isEmpty() && mediaType == mailio::mime::media_type_t::TEXT && subtype == "plain") {
        const std::string body = part.content();
        const QString text = QString::fromUtf8(body.c_str(), static_cast<int>(body.size())).trimmed();
        if (!text.isEmpty()) {
            plainOut = text;
        }
    }

    if (mediaType == mailio::mime::media_type_t::IMAGE) {
        const std::string cidRaw = part.content_id();
        QString cid = normalizeCid(QString::fromStdString(cidRaw));
        if (cid.isEmpty()) return;

        const std::string rawContent = part.content();
        if (rawContent.empty()) return;
        const QByteArray bytes(rawContent.data(), static_cast<int>(rawContent.size()));
        if (bytes.isEmpty()) return;

        const QString subtypeQt = QString::fromStdString(subtype).toLower();
        const QString mimeType = subtypeQt.isEmpty()
                ? QStringLiteral("image/jpeg")
                : QStringLiteral("image/") + subtypeQt;
        const QString dataUrl = QStringLiteral("data:%1;base64,%2").arg(mimeType, QString::fromLatin1(bytes.toBase64()));
        cidMap.insert(cid, dataUrl);
    }
}

QString inlineCidImages(QString html, const QHash<QString, QString> &cidMap)
{
    if (html.isEmpty() || cidMap.isEmpty()) return html;
    for (auto it = cidMap.constBegin(); it != cidMap.constEnd(); ++it) {
        const QString cid = it.key();
        const QString data = it.value();
        html.replace(QStringLiteral("cid:%1").arg(cid), data, Qt::CaseInsensitive);
        html.replace(QStringLiteral("cid:<%1>").arg(cid), data, Qt::CaseInsensitive);
    }
    return html;
}

QString sanitizeExtractedHtml(QString html)
{
    html = html.trimmed();
    if (html.isEmpty()) return {};

    // Note: do not perform ad-hoc quoted-printable decode here.
    // mailio part decoding already handles transfer encoding; extra decode corrupts valid '=' in HTML/URLs.

    const QString lower = html.toLower();
    const int htmlStart = lower.indexOf(QStringLiteral("<html"));
    if (htmlStart > 0) html = html.mid(htmlStart);

    const QString lower2 = html.toLower();
    const int htmlEnd = lower2.indexOf(QStringLiteral("</html>"));
    if (htmlEnd >= 0) {
        html = html.left(htmlEnd + 7);
    }

    const QString check = html.toLower();
    if (check.contains(QStringLiteral("delivered-to:")) && check.contains(QStringLiteral("received:"))
            && !check.contains(QStringLiteral("<body"))) {
        return {};
    }

    if (!html.contains(QRegularExpression(QStringLiteral("<html|<body|<div|<table|<p|<br|<span|<img|<a\\b"),
                                          QRegularExpression::CaseInsensitiveOption))) {
        return {};
    }
    return html.trimmed();
}

} // namespace

QString extractHtmlWithMailio(const QByteArray &fetchRespRaw)
{
    try {
        const QByteArray rfc822 = extractRfc822Literal(fetchRespRaw);
        if (rfc822.isEmpty()) {
            qWarning().noquote() << "[mime] no-rfc822-literal fetched=" << fetchRespRaw.size() << "bytes";
            return {};
        }
        mailio::message msg;
        msg.parse(truncateLongHeaderLines(rfc822).toStdString(), false);

        QString html;
        QString plain;
        QHash<QString, QString> cidMap;
        collectMimeParts(msg, html, plain, cidMap);

        if (html.trimmed().isEmpty()) {
            // Some providers keep HTML in top-level content.
            const std::string top = msg.content();
            html = QString::fromUtf8(top.c_str(), static_cast<int>(top.size())).trimmed();
        }

        html = inlineCidImages(html, cidMap);
        html = sanitizeExtractedHtml(html);
        return html;
    } catch (const std::exception &e) {
        qWarning().noquote() << "[mime] mailio-html-exception:" << e.what();
        return {};
    } catch (...) {
        qWarning().noquote() << "[mime] mailio-html-exception: unknown";
        return {};
    }
}

QString extractPlainTextWithMailio(const QByteArray &fetchRespRaw)
{
    try {
        const QByteArray rfc822 = extractRfc822Literal(fetchRespRaw);
        if (rfc822.isEmpty()) return {};

        mailio::message msg;
        msg.parse(truncateLongHeaderLines(rfc822).toStdString(), false);

        QString html;
        QString plain;
        QHash<QString, QString> cidMap;
        collectMimeParts(msg, html, plain, cidMap);

        if (plain.trimmed().isEmpty()) {
            const std::string top = msg.content();
            plain = QString::fromUtf8(top.c_str(), static_cast<int>(top.size())).trimmed();
        }
        return plain.trimmed();
    } catch (const std::exception &e) {
        qWarning().noquote() << "[mime] mailio-plain-exception:" << e.what();
        return {};
    } catch (...) {
        qWarning().noquote() << "[mime] mailio-plain-exception: unknown";
        return {};
    }
}

QVariantList extractAttachmentsWithMailio(const QByteArray &fetchRespRaw)
{
    QVariantList out;
    try {
        const QByteArray rfc822 = extractRfc822Literal(fetchRespRaw);
        if (rfc822.isEmpty())
            return out;

        mailio::message msg;
        msg.parse(truncateLongHeaderLines(rfc822).toStdString(), false);

        int index = 0;
        std::function<void(const mailio::mime&)> walk = [&](const mailio::mime &part) {
            auto &mutablePart = const_cast<mailio::mime&>(part);
            const auto ct = mutablePart.content_type();
            const auto mediaType = ct.media_type();
            const auto subtype = QString::fromStdString(ct.media_subtype()).toLower();

            const QString fileName = QString::fromStdString(mutablePart.name()).trimmed();
            const std::string body = part.content();
            const int sizeBytes = static_cast<int>(body.size());

            const bool hasFileName = !fileName.isEmpty();
            const bool nonTextLeaf = mediaType != mailio::mime::media_type_t::TEXT;
            if (hasFileName || nonTextLeaf) {
                QVariantMap row;
                row.insert("index", index++);
                row.insert("name", hasFileName ? fileName : QStringLiteral("Attachment"));
                QString mediaName = "application";
                switch (mediaType) {
                    case mailio::mime::media_type_t::TEXT: mediaName = "text"; break;
                    case mailio::mime::media_type_t::IMAGE: mediaName = "image"; break;
                    case mailio::mime::media_type_t::AUDIO: mediaName = "audio"; break;
                    case mailio::mime::media_type_t::VIDEO: mediaName = "video"; break;
                    case mailio::mime::media_type_t::APPLICATION: mediaName = "application"; break;
                    case mailio::mime::media_type_t::MULTIPART: mediaName = "multipart"; break;
                    case mailio::mime::media_type_t::MESSAGE: mediaName = "message"; break;
                    default: break;
                }
                row.insert("mimeType", QStringLiteral("%1/%2").arg(mediaName, subtype));
                row.insert("bytes", sizeBytes);
                row.insert("canPreview", mediaType == mailio::mime::media_type_t::IMAGE);
                out.push_back(row);
            }

            for (const auto &child : part.parts())
                walk(child);
        };

        walk(msg);
        return out;
    } catch (...) {
        return out;
    }
}

QByteArray extractAttachmentDataWithMailio(const QByteArray &fetchRespRaw, const int index)
{
    try {
        const QByteArray rfc822 = extractRfc822Literal(fetchRespRaw);
        if (rfc822.isEmpty())
            return {};

        mailio::message msg;
        msg.parse(truncateLongHeaderLines(rfc822).toStdString(), false);

        int current = 0;
        QByteArray out;
        std::function<void(const mailio::mime&)> walk = [&](const mailio::mime &part) {
            if (!out.isEmpty())
                return;

            auto &mutablePart = const_cast<mailio::mime&>(part);
            const auto ct = mutablePart.content_type();
            const auto mediaType = ct.media_type();
            const QString fileName = QString::fromStdString(mutablePart.name()).trimmed();
            const bool hasFileName = !fileName.isEmpty();
            const bool nonTextLeaf = mediaType != mailio::mime::media_type_t::TEXT;

            if (hasFileName || nonTextLeaf) {
                if (current == index) {
                    const std::string body = part.content();
                    out = QByteArray(body.data(), static_cast<int>(body.size()));
                    return;
                }
                ++current;
            }

            for (const auto &child : part.parts())
                walk(child);
        };

        walk(msg);
        return out;
    } catch (...) {
        return {};
    }
}

} // namespace Mime
