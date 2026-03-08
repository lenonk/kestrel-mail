#include "bodyprocessor.h"
#include "snippetutils.h"
#include "../../../mime/mailioparser.h"
#include <QRegularExpression>
#include <QTextDocument>
#include <algorithm>
#include <utility>

#include "src/core/mime/mailioparser.h"

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

    // params, id, desc, encoding, size, ...
    QString charset;
    skipWs();

    if (m_i < m_text.size() && m_text[m_i] == '(') {
        const auto start = m_i;
        qint32 depth = 0;

        do {
            if (m_text[m_i] == '(') ++depth;
            else if (m_text[m_i] == ')') --depth;
            ++m_i;
        } while (m_i < m_text.size() && depth > 0);

        const auto paramsRaw = m_text.mid(start, m_i - start).toLower();

        static const QRegularExpression re(QStringLiteral("charset\\s*\\\"?\\s*([a-z0-9._-]+)"));

        if (const auto mm = re.match(paramsRaw); mm.hasMatch())
            charset = mm.captured(1).trimmed();
    }
    else {
        skipAny();
    }

    skipAny();
    skipAny();

    const auto encoding = parseAtomOrQuoted().toLower();
    const auto sizeToken = parseAtomOrQuoted();

    if (type == QStringLiteral("TEXT")) {
        const auto pid = partPrefix.isEmpty() ? QStringLiteral("1") : partPrefix;
        BodyPart part;

        part.partId = pid;
        part.type = type;
        part.subtype = subtype;
        part.encoding = encoding;
        part.charset = charset.isEmpty() ? QStringLiteral("utf-8") : charset;

        auto okBytes = false;
        part.bytes = sizeToken.toInt(&okBytes);

        if (!okBytes)
            part.bytes = 0;

        int score = 0;
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
extractBodySnippetFromFetch(const QByteArray &fetchRespRaw) {
    QString best;
    int bestScore = -999;

    qsizetype i = 0;
    while (i < fetchRespRaw.size()) {
        if (fetchRespRaw[i] != '{') {
            ++i;
            continue;
        }

        qsizetype j = i + 1;
        while (j < fetchRespRaw.size() && fetchRespRaw[j] >= '0' && fetchRespRaw[j] <= '9') {
            ++j;
        }
        if (j <= i + 1 || fetchRespRaw[j] != '}') {
            ++i;
            continue;
        }

        auto literalStart = j + 1;
        if (literalStart < fetchRespRaw.size() && ((fetchRespRaw[literalStart] == '\r') || fetchRespRaw[literalStart] == '\n'))
            ++literalStart;

        auto ok = false;
        const auto literalLen = fetchRespRaw.mid(i + 1, j - (i + 1)).toInt(&ok);
        if (!ok || literalLen <= 0) {
            ++i;
            continue;
        }

        if (literalStart >= fetchRespRaw.size()) break;

        const auto literalEndExclusive = qMin(fetchRespRaw.size(), literalStart + literalLen);
        if (literalEndExclusive <= literalStart) {
            i = j + 1;
            continue;
        }

        auto literalBytes = fetchRespRaw.mid(literalStart, literalEndExclusive - literalStart);
        if (auto bodyStart = literalBytes.indexOf("\r\n\r\n"); bodyStart >= 0) {
            literalBytes = literalBytes.mid(bodyStart + 4);
        }
        else {
            bodyStart = literalBytes.indexOf("\n\n");
            if (bodyStart >= 0) {
                literalBytes = literalBytes.mid(bodyStart + 2);
            }
        }

        const auto decoded = decodeTransferEncoded(literalBytes);
        auto cand = SnippetUtils::cleanSnippet(QString::fromUtf8(decoded)).left(180);
        if (cand.isEmpty())
            cand = SnippetUtils::cleanSnippet(QString::fromUtf8(literalBytes)).left(180);

        if (const auto score = SnippetUtils::snippetQualityScore(cand); !cand.isEmpty() && score > bestScore) {
            best = cand;
            bestScore = score;
        }

        i = literalEndExclusive;
    }

    if (best.isEmpty()) {
        best = SnippetUtils::cleanSnippet(QString::fromUtf8(fetchRespRaw)).left(180);
    }
    return best;
}

QString
extractBodyHtmlFromFetch(const QByteArray &fetchRespRaw) {
    auto html = Mime::extractHtmlWithMailio(fetchRespRaw).trimmed();
    if (!html.isEmpty()) { return html; }

    if (const QString plain = Mime::extractPlainTextWithMailio(fetchRespRaw).trimmed(); !plain.isEmpty()) {
        QString escaped = plain.toHtmlEscaped();
        escaped.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        escaped.replace(QStringLiteral("\r"), QStringLiteral("\n"));
        escaped.replace(QStringLiteral("\n"), QStringLiteral("<br/>\n"));
        return QStringLiteral("<html><body style=\"white-space:normal;\">%1</body></html>").arg(escaped);
    }

    qWarning().noquote() << "[mime] mailio_extract_failed (attachment-only or unrecognised structure)";
    return QStringLiteral(
        "<html><body style=\"font-family:sans-serif;padding:1.5em;color:#aaa;\">"
        "<p style=\"margin:0;\">(This message has no text content — it may contain only attachments.)</p>"
        "</body></html>");
}

QString
extractBodyTextForSnippet(const QByteArray &fetchRespRaw) {
    // Plain text is already clean — try it first.
    const QString plain = Mime::extractPlainTextWithMailio(fetchRespRaw).trimmed();
    if (!plain.isEmpty())
        return plain;

    // For HTML emails: strip tags aggressively, and prefer hidden preheader text
    // (display:none spans that newsletters use to set the inbox preview line).
    const QString html = Mime::extractHtmlWithMailio(fetchRespRaw).trimmed();
    if (html.isEmpty())
        return {};  // mailio failed entirely — caller should try raw literal paths

    const auto preheader = extractHiddenPreheader(html);
    if (!preheader.isEmpty())
        return preheader;

    return stripHtmlTags(html);
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

QString
stripHtmlTags(const QString &html) {
    auto s = html;

    static const QRegularExpression htmlHeadRe(QStringLiteral("<head[\\s\\S]*?</head>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlStyleRe(QStringLiteral("<style[^>]*>[\\s\\S]*?</style>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlScriptRe(QStringLiteral("<script[^>]*>[\\s\\S]*?</script>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlBodyStyleRe(QStringLiteral("body\\s*\\{[^}]*\\}"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlBreakTagsRe(QStringLiteral("<br\\s*/?>|</p>|</div>|</li>|</tr>|</h[1-6]>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression htmlTagRe(QStringLiteral("<[^>]+>"));
    static const QRegularExpression htmlEntityRe(QStringLiteral("&nbsp;|&amp;|&[a-zA-Z0-9#]+;"));
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));

    s.replace(htmlHeadRe, QStringLiteral(" "));
    s.replace(htmlStyleRe, QStringLiteral(" "));
    s.replace(htmlScriptRe, QStringLiteral(" "));
    s.replace(htmlBodyStyleRe, QStringLiteral(" "));

    s.replace(htmlBreakTagsRe, QStringLiteral("\n"));
    s.replace(htmlTagRe, QStringLiteral(" "));
    s.replace(htmlEntityRe, QStringLiteral(" "));
    s.replace(whitespaceRe, QStringLiteral(" "));

    return s.trimmed();
}

QString
decodeSnippetLiteral(const QByteArray &literalBytes, const BodyPart &meta) {
    auto decoded = literalBytes;

    if (const auto enc = meta.encoding.toLower(); enc == QStringLiteral("base64")) {
        auto payload = literalBytes;

        payload.replace("\r", "");
        payload.replace("\n", "");

        if (const auto b = QByteArray::fromBase64(payload); !b.isEmpty()) {
            decoded = b;
        }
    }
    else if (enc == QStringLiteral("quoted-printable")) {
        decoded = decodeQuotedPrintable(literalBytes);
    }

    QString text;
    if (const auto cs = meta.charset.toLower(); cs == QStringLiteral("iso-8859-1") || cs == QStringLiteral("windows-1252")) {
        text = QString::fromLatin1(decoded);
    }
    else {
        text = QString::fromUtf8(decoded);
    }

    if (meta.subtype.toLower() == QStringLiteral("html") || text.contains('<')) {
        const auto preheader = extractHiddenPreheader(text);
        const auto bodyText = stripHtmlTags(text);

        if (!preheader.isEmpty()) {
            text = preheader;
            if (!bodyText.isEmpty() && !bodyText.startsWith(preheader, Qt::CaseInsensitive)) {
                text = (preheader + QStringLiteral(" ") + bodyText).left(220);
            }
        }
        else {
            text = bodyText;
        }
    }
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));
    text.replace(whitespaceRe, QStringLiteral(" "));
    return text.trimmed();
}

} // namespace Imap::BodyProcessor

