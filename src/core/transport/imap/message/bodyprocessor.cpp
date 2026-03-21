#include "bodyprocessor.h"
#include "../../../mime/mailioparser.h"
#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;

namespace Imap::BodyProcessor {

namespace Private_ {

BodyStructureParser::BodyStructureParser(QString  text) : m_text(std::move(text)) {}

QList<BodyPart>
BodyStructureParser::parsePreferredTextParts() {
    QList<BodyPart> out;

    auto pos = m_text.indexOf(QStringLiteral("BODYSTRUCTURE"));
    if (pos < 0)
        return out;

    pos = m_text.indexOf('(', pos);
    if (pos < 0)
        return out;

    m_i = pos;

    parseBody(QString(), out);

    std::ranges::sort(out, [](const BodyPart &a, const BodyPart &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.partId < b.partId;
    });

    return out;
}

void
BodyStructureParser::skipWs() {
    while (m_i < m_text.size() && m_text[m_i].isSpace())
        ++m_i;
}

bool
BodyStructureParser::consume(const QChar c) {
    skipWs();
    if (m_i < m_text.size() && m_text[m_i] == c) {
        ++m_i; return true;
    }

    return false;
}

QString
BodyStructureParser::parseAtomOrQuoted() {
    skipWs();

    if (m_i >= m_text.size())
        return {};

    if (m_text[m_i] == '"') {
        ++m_i;
        QString out;
        while (m_i < m_text.size()) {
            const QChar ch = m_text[m_i++];
            if (ch == '"') break;
            if (ch == '\\' && m_i < m_text.size()) out += m_text[m_i++];
            else out += ch;
        }

        return out;
    }

    const auto start = m_i;
    while (m_i < m_text.size() && !m_text[m_i].isSpace() && m_text[m_i] != '(' && m_text[m_i] != ')')
        ++m_i;

    return m_text.mid(start, m_i - start);
}

void
BodyStructureParser::skipAny() {
    skipWs();

    if (m_i >= m_text.size())
        return;

    if (m_text[m_i] == '(') {
        qint32 depth = 0;
        do {
            if (m_text[m_i] == '(')
                ++depth;
            else if (m_text[m_i] == ')')
                --depth;
            ++m_i;
        } while (m_i < m_text.size() && depth > 0);

        return;
    }

    (void)parseAtomOrQuoted();
}

void
BodyStructureParser::skipRemainingInList() {
    while (m_i < m_text.size()) {
        skipWs();
        if (m_i < m_text.size() && m_text[m_i] == ')') {
            ++m_i; break;
        }
        skipAny();
    }
}

void
BodyStructureParser::parseMultipart(const QString &partPrefix, QList<BodyPart> &out) {
    int idx = 1;
    while (m_i < m_text.size() && m_text[m_i] == '(') {
        const auto childId = partPrefix.isEmpty() ? QString::number(idx) : (partPrefix + "." + QString::number(idx));
        parseBody(childId, out);
        ++idx;
        skipWs();
    }

    const auto subtype = parseAtomOrQuoted().toUpper();
    skipRemainingInList();

    // slight preference for multipart/alternative first part
    if (subtype == QStringLiteral("ALTERNATIVE")) {
        for (auto &p : out) {
            if (p.partId.startsWith(partPrefix.isEmpty() ? QStringLiteral("1") : (partPrefix + "."))) {
                p.score += 2;
            }
        }
    }
}

void
BodyStructureParser::parseSinglepart(const QString &partPrefix, QList<BodyPart> &out) {
    const auto type = parseAtomOrQuoted().toUpper();
    const auto subtype = parseAtomOrQuoted().toUpper();

    // BODYSTRUCTURE param format: ("KEY" "VALUE" "KEY2" "VALUE2" ...)
    // Both key and value are quoted atoms separated by whitespace.
    static const QRegularExpression charsetRe(
        QStringLiteral("\"charset\"\\s+\"([a-z0-9._-]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nameRe(
        QStringLiteral("\"(?:name|filename)\"\\s+\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);

    // Helper: extract paren-delimited block at current position, preserving original case.
    auto readParenBlock = [&]() -> QString {
        skipWs();
        if (m_i >= m_text.size() || m_text[m_i] != '(') return {};
        const auto start = m_i;
        qint32 depth = 0;
        do {
            if (m_text[m_i] == '(') ++depth;
            else if (m_text[m_i] == ')') --depth;
            ++m_i;
        } while (m_i < m_text.size() && depth > 0);
        // Preserve original casing (especially filenames); regexes are case-insensitive.
        return m_text.mid(start, m_i - start);
    };

    QString charset;
    QString filename;
    skipWs();

    if (m_i < m_text.size() && m_text[m_i] == '(') {
        const QString paramsRaw = readParenBlock();
        if (const auto mm = charsetRe.match(paramsRaw); mm.hasMatch())
            charset = mm.captured(1).trimmed();
        if (const auto fm = nameRe.match(paramsRaw); fm.hasMatch())
            filename = fm.captured(1).trimmed();
    } else {
        skipAny(); // NIL
    }

    skipAny(); // body-id
    skipAny(); // body-description

    const auto encoding = parseAtomOrQuoted().toLower();
    const auto sizeToken = parseAtomOrQuoted();

    // TEXT parts have an extra 'lines' field after size.
    if (type == QStringLiteral("TEXT"))
        skipAny();

    // Optional md5: atom or quoted string (never a list).
    // If the next non-ws token is '(' it must be disposition — don't consume it as md5.
    skipWs();
    if (m_i < m_text.size() && m_text[m_i] != '(' && m_text[m_i] != ')')
        skipAny();

    // Content-Disposition: always parse — type (inline vs attachment) affects isAttachment.
    bool isInlineDisp = false;
    {
        const QString dispRaw = readParenBlock();
        if (!dispRaw.isEmpty()) {
            static const QRegularExpression dispTypeRe(
                QStringLiteral("^\\(\\s*\"([^\"]+)\""),
                QRegularExpression::CaseInsensitiveOption);
            if (const auto dm = dispTypeRe.match(dispRaw); dm.hasMatch())
                isInlineDisp = dm.captured(1).compare(QStringLiteral("inline"), Qt::CaseInsensitive) == 0;
            if (filename.isEmpty()) {
                if (const auto fm = nameRe.match(dispRaw); fm.hasMatch())
                    filename = fm.captured(1).trimmed();
            }
        }
    }

    {
        const auto pid = partPrefix.isEmpty() ? QStringLiteral("1") : partPrefix;
        BodyPart part;

        part.partId = pid;
        part.type = type;
        part.subtype = subtype;
        part.encoding = encoding;
        part.charset = charset.isEmpty() ? QStringLiteral("utf-8") : charset;
        part.filename = filename;
        part.isInline = isInlineDisp;

        if (!part.filename.isEmpty() && !isInlineDisp)
            part.isAttachment = true;

        auto okBytes = false;
        part.bytes = sizeToken.toInt(&okBytes);

        if (!okBytes)
            part.bytes = 0;

        int score = 0;
        if (type == QStringLiteral("TEXT")) {
            if (subtype == QStringLiteral("PLAIN"))
                score += 300;
            else if (subtype == QStringLiteral("HTML"))
                score += 200;
            else
                score += 100;

            if (!part.isAttachment)
                score += 50;

            if (part.charset == QStringLiteral("utf-8"))
                score += 20;

            if (encoding == QStringLiteral("quoted-printable") || encoding == QStringLiteral("7bit") || encoding == QStringLiteral("8bit"))
                score += 10;
        }

        part.score = score;
        out.push_back(part);
    }
}

void
BodyStructureParser::parseBody(const QString &partPrefix, QList<BodyPart> &out) {
    if (!consume('(')) return;

    skipWs();

    // multipart: starts with nested body parts
    if (m_i < m_text.size() && m_text[m_i] == '(') {
        parseMultipart(partPrefix, out);
        return;
    }

    // single part
    parseSinglepart(partPrefix, out);

    skipRemainingInList();
}

} // anonymous namespace

/*
 * Begin public API
 */

QList<BodyPart>
parsePreferredTextParts(const QString &bodyStructureResponse) {
    Private_::BodyStructureParser parser(bodyStructureResponse);
    return parser.parsePreferredTextParts();
}

QStringList
preferredSnippetPartIds(const QString &bodyStructureResponse) {
    QStringList out;

    for (const auto parts = parsePreferredTextParts(bodyStructureResponse); const BodyPart &p : parts) {
        if (!p.partId.isEmpty() && !out.contains(p.partId)) {
            out.push_back(p.partId);
        }
    }
    return out;
}

QList<BodyPart>
parseAttachmentParts(const QString &bodyStructureResponse) {
    QList<BodyPart> all = parsePreferredTextParts(bodyStructureResponse);
    QList<BodyPart> out;
    for (const BodyPart &p : all) {
        if (p.isInline)
            continue;
        if (p.isAttachment
                || (p.type != QStringLiteral("TEXT")
                    && p.type != QStringLiteral("MULTIPART")
                    && !p.type.isEmpty())) {
            out.push_back(p);
        }
    }
    return out;
}

BodyPart
preferredSnippetPart(const QString &bodyStructureResponse) {
    const QList<BodyPart> parts = parsePreferredTextParts(bodyStructureResponse);
    if (parts.isEmpty()) return BodyPart{};

    BodyPart plain;
    BodyPart html;

    for (const BodyPart &p : parts) {
        if (plain.partId.isEmpty() && p.subtype == QStringLiteral("PLAIN")) plain = p;
        if (html.partId.isEmpty() && p.subtype == QStringLiteral("HTML")) html = p;
    }

    // Replies often have tiny plain parts; prefer HTML if plain is suspiciously short.
    if (!plain.partId.isEmpty() && !html.partId.isEmpty() && plain.bytes > 0 && plain.bytes < 150) {
        return html;
    }

    return parts.first();
}

QByteArray
decodeQuotedPrintable(const QByteArray &in) {
    QByteArray out;

    out.reserve(in.size());
    for (qint32 i = 0; i < in.size(); ++i) {
        const auto c = in.at(i);
        if (c == '=' && i + 1 < in.size()) {
            if (i + 2 < in.size() && in.at(i + 1) == '\r' && in.at(i + 2) == '\n') {
                i += 2;
                continue;
            }

            if (in.at(i + 1) == '\n') {
                i += 1;
                continue;
            }

            if (i + 2 < in.size()) {
                auto ok = false;
                const auto value = in.mid(i + 1, 2).toInt(&ok, 16);
                if (ok) {
                    out.append(static_cast<char>(value));
                    i += 2;
                    continue;
                }
            }
        }
        out.append(c);
    }
    return out;
}

QByteArray
decodeTransferEncoded(const QByteArray &raw) {
    const auto lower = raw.toLower();

    auto stripPayload = [&](const QByteArray &src) {
        auto start = src.indexOf("\r\n\r\n");
        if (start >= 0) {
            start += 4;
        }
        else {
            start = src.indexOf("\n\n");
            if (start >= 0)
                start += 2;
        }

        QByteArray payload = start > 0 ? src.mid(start) : src;
        payload.replace("\r", "");
        payload.replace("\n", "");
        return payload;
    };

    if (lower.contains("content-transfer-encoding: base64")) {
        const auto payload = stripPayload(raw);
        if (const auto decoded = QByteArray::fromBase64(payload); !decoded.isEmpty())
            return decoded;
    }

    if (lower.contains("content-transfer-encoding: quoted-printable")) {
        return decodeQuotedPrintable(raw);
    }

    // Avoid aggressive blind base64 decode here; it creates binary garbage for many
    // newsletter/plaintext payloads that merely look base64-ish.
    return decodeQuotedPrintable(raw);
}

QString
extractBodyHtmlFromFetch(const QByteArray &fetchRespRaw) {
    auto html = Mime::extractHtmlWithMailio(fetchRespRaw).trimmed();
    if (!html.isEmpty()) { return html; }

    // Heuristic fallback: some malformed MIME payloads still contain a usable
    // raw HTML document that mailio misses. Recover the largest <html>...</html>
    // block before dropping to plain-text wrapping.
    const QString rawUtf8 = QString::fromUtf8(fetchRespRaw);
    const QString rawLatin1 = QString::fromLatin1(fetchRespRaw);
    auto extractRawHtmlDoc = [](const QString &raw) -> QString {
        const int start = raw.indexOf(QStringLiteral("<html"), 0, Qt::CaseInsensitive);
        if (start < 0)
            return {};
        const int end = raw.lastIndexOf(QStringLiteral("</html>"), -1, Qt::CaseInsensitive);
        if (end <= start)
            return {};
        const int endIncl = end + QStringLiteral("</html>").size();
        const QString doc = raw.mid(start, endIncl - start).trimmed();
        return doc.size() >= 256 ? doc : QString();
    };

    html = extractRawHtmlDoc(rawUtf8);
    if (html.isEmpty())
        html = extractRawHtmlDoc(rawLatin1);
    if (!html.isEmpty())
        return html;

    if (const QString plain = Mime::extractPlainTextWithMailio(fetchRespRaw).trimmed(); !plain.isEmpty()) {
        QString escaped = plain.toHtmlEscaped();
        escaped.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        escaped.replace(QStringLiteral("\r"), QStringLiteral("\n"));
        escaped.replace(QStringLiteral("\n"), QStringLiteral("<br/>\n"));
        return QStringLiteral("<html><body style=\"white-space:normal;\">%1</body></html>").arg(escaped);
    }

    return QStringLiteral(
        "<html><body style=\"font-family:sans-serif;padding:1.5em;color:#aaa;\">"
        "<p style=\"margin:0;\">(This message has no text content — it may contain only attachments.)</p>"
        "</body></html>");
}

// Remove a complete <tag>…</tag> block using a linear string scan.
// Avoids PCRE backtracking limits that cause regex to fail on large HTML.
static void stripHtmlBlock(QString &s, QLatin1StringView tag)
{
    QString openPfx; openPfx.reserve(tag.size() + 1); openPfx += u'<'; openPfx += tag;
    QString closeTag; closeTag.reserve(tag.size() + 2); closeTag += u'<'; closeTag += u'/'; closeTag += tag;
    int i = 0;
    while (i < s.size()) {
        const int start = s.indexOf(openPfx, i, Qt::CaseInsensitive);
        if (start < 0) break;
        const int afterTag = start + openPfx.size();
        if (afterTag < s.size()) {
            const QChar c = s[afterTag];
            // Must be >, /, or whitespace — not a longer tag name like <header>
            if (c != u'>' && c != u'/' && !c.isSpace()) { i = start + 1; continue; }
        }
        const int closeStart = s.indexOf(closeTag, start, Qt::CaseInsensitive);
        if (closeStart < 0) { s.truncate(start); break; }
        const int closeEnd = s.indexOf(u'>', closeStart);
        if (closeEnd < 0) { s.truncate(start); break; }
        s.remove(start, closeEnd - start + 1);
    }
}

// Flatten HTML to plain text for snippet generation.
// Uses string-based block removal (no regex backtracking) for <head>/<style>/<script>.
static QString flattenHtmlToText(const QString &html)
{
    auto s = html;
    stripHtmlBlock(s, "head"_L1);
    stripHtmlBlock(s, "style"_L1);
    stripHtmlBlock(s, "script"_L1);

    // Block-end tags → newline so words don't jam together
    static const QRegularExpression kBreakTagsRe(
        QStringLiteral("<br\\s*/?>|</p>|</div>|</li>|</tr>|</h[1-6]>"),
        QRegularExpression::CaseInsensitiveOption);
    s.replace(kBreakTagsRe, "\n"_L1);

    // Strip remaining tags
    static const QRegularExpression kTagRe(QStringLiteral("<[^>]+>"));
    s.replace(kTagRe, " "_L1);

    // Decode common HTML entities
    s.replace("&nbsp;"_L1,  " "_L1,        Qt::CaseInsensitive);
    s.replace("&amp;"_L1,   "&"_L1,        Qt::CaseInsensitive);
    s.replace("&lt;"_L1,    "<"_L1,        Qt::CaseInsensitive);
    s.replace("&gt;"_L1,    ">"_L1,        Qt::CaseInsensitive);
    s.replace("&#39;"_L1,   "'"_L1,        Qt::CaseInsensitive);
    s.replace("&quot;"_L1,  "\""_L1,       Qt::CaseInsensitive);
    s.replace("&ndash;"_L1, u"\u2013"_s,   Qt::CaseInsensitive);
    s.replace("&mdash;"_L1, u"\u2014"_s,   Qt::CaseInsensitive);
    s.replace("&hellip;"_L1,u"\u2026"_s,   Qt::CaseInsensitive);
    s.replace("&rsquo;"_L1, u"\u2019"_s,   Qt::CaseInsensitive);
    s.replace("&lsquo;"_L1, u"\u2018"_s,   Qt::CaseInsensitive);
    s.replace("&rdquo;"_L1, u"\u201D"_s,   Qt::CaseInsensitive);
    s.replace("&ldquo;"_L1, u"\u201C"_s,   Qt::CaseInsensitive);

    static const QRegularExpression kWsRe(QStringLiteral("\\s+"));
    s.replace(kWsRe, " "_L1);
    return s.trimmed();
}

// Clean up plain-text content before using it as a snippet.
// Skips forwarded-message preamble and leading separator lines.
static QString cleanPlainTextForSnippet(const QString &text)
{
    // Skip forwarded-message header block (e.g. "---------- Forwarded message ---------")
    const int fwdIdx = text.indexOf("forwarded message"_L1, 0, Qt::CaseInsensitive);
    if (fwdIdx >= 0 && fwdIdx < 200) {
        const int blankLine = text.indexOf("\n\n"_L1, fwdIdx);
        if (blankLine >= 0) {
            const auto after = text.mid(blankLine).trimmed();
            if (after.size() > 20) return after;
        }
    }
    // Strip leading separator-only lines (---, ===, ***, etc.)
    int pos = 0;
    while (pos < text.size()) {
        const int lineEnd = text.indexOf('\n', pos);
        if (lineEnd < 0) break;
        bool isSep = (lineEnd - pos) >= 3;
        for (int j = pos; isSep && j < lineEnd; ++j) {
            const auto u = text[j].unicode();
            isSep = (u == '-' || u == '=' || u == '_' || u == '*' || u == ' ' || u == '\r');
        }
        if (!isSep) break;
        pos = lineEnd + 1;
    }
    if (pos > 0) {
        const auto stripped = text.mid(pos).trimmed();
        if (!stripped.isEmpty()) return stripped;
    }
    return text;
}

QString
extractBodyTextForSnippet(const QByteArray &fetchRespRaw) {
    // Plain text is preferred, but NOT when mailio's msg.content() fallback has returned
    // raw HTML (happens for HTML-only messages with no text/plain part).
    const QString plain = Mime::extractPlainTextWithMailio(fetchRespRaw).trimmed();
    if (!plain.isEmpty() && !plain.startsWith(u'<')) {
        // Reject if the content looks like CSS leaking through (3+ braces in first 500 chars)
        int braceCount = 0;
        const int checkLen = qMin(plain.size(), 500);
        for (int k = 0; k < checkLen; ++k) {
            if (plain[k] == u'{' || plain[k] == u'}') ++braceCount;
        }
        if (braceCount < 3)
            return cleanPlainTextForSnippet(plain);
    }

    // HTML path: use the element-aware flattener
    QString html = Mime::extractHtmlWithMailio(fetchRespRaw).trimmed();
    // If mailio gave us nothing but "plain" is actually HTML, use it directly
    if (html.isEmpty() && !plain.isEmpty() && plain.startsWith(u'<'))
        html = plain;
    if (html.isEmpty()) return {};

    // Prefer hidden preheader text (display:none preview spans used by newsletters)
    const auto preheader = extractHiddenPreheader(html);
    if (!preheader.isEmpty()) return preheader;

    return flattenHtmlToText(html);
}

QString
extractHiddenPreheader(const QString &html) {
    static const QRegularExpression preheaderRe(
            QStringLiteral("<(?:div|span|p)[^>]*style=[\"'][^\"']*(?:display\\s*:\\s*none|visibility\\s*:\\s*hidden|mso-hide\\s*:\\s*all)[^\"']*[\"'][^>]*>([\\s\\S]{1,400}?)</(?:div|span|p)>"),
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlTagRe (QStringLiteral("<[^>]+>"));
    static const QRegularExpression htmlEntityRe (QStringLiteral("&nbsp;|&amp;|&[a-zA-Z0-9#]+;"));
    static const QRegularExpression quotPrintRe (QStringLiteral("(=[0-9A-Fa-f]{2})+"));
    static const QRegularExpression quotPrint2Re (QStringLiteral("=09|=20"));
    static const QRegularExpression spacesRe (QStringLiteral("\\s+"));

    const auto m = preheaderRe.match(html);
    if (!m.hasMatch()) return {};

    auto pre = m.captured(1);

    pre.replace(htmlTagRe, QStringLiteral(" "));
    pre.replace(htmlEntityRe, QStringLiteral(" "));
    pre.replace(quotPrintRe, QStringLiteral(" "));
    pre.replace(quotPrint2Re, QStringLiteral(" "));
    pre.replace(spacesRe, QStringLiteral(" "));

    pre = pre.trimmed();
    if (pre.size() > 200)
        pre = pre.left(200);

    return pre;
}

} // namespace Imap::BodyProcessor

