#include "messageutils.h"

#include "../parser/responseparser.h"
#include "bodyprocessor.h"

#include <QRegularExpression>
#include <QUrl>

using namespace Qt::Literals::StringLiterals;

namespace Imap::MessageUtils {

namespace {

static const QRegularExpression kRfc2047Re(QStringLiteral("=\\?([^?]+)\\?([bBqQ])\\?([^?]+)\\?="));
static const QRegularExpression kWhitespaceRe(QStringLiteral("\\s+"));

// Collapse all whitespace (including Unicode spaces) to single ASCII spaces,
// trim leading/trailing. Mirrors mailcore2's MCString::stripWhitespace().
static QString stripWhitespace(const QString &in) {
    QString out; out.reserve(in.size());
    for (const auto ch : in) {
        const auto u = ch.unicode();
        if (u == '\t' || u == '\n' || u == '\f' || u == '\r'
                || u == 160 || u == 133 || u == 0x2028 || u == 0x2029)
            out.append(' ');
        else
            out.append(ch);
    }
    // Collapse runs of spaces
    QString collapsed; collapsed.reserve(out.size());
    bool prevSpace = false;
    for (const auto ch : out) {
        const bool isSpace = (ch == ' ');
        if (isSpace && prevSpace) continue;
        collapsed.append(ch);
        prevSpace = isSpace;
    }
    return collapsed.trimmed();
}

} // namespace

// ── Address helpers ───────────────────────────────────────────────────────────

QString cleanAngle(const QString &s) {
    auto out = s.trimmed();
    if (out.startsWith('<') && out.endsWith('>') && out.size() > 2)
        out = out.mid(1, out.size() - 2);
    return out;
}

QString extractEmailAddress(const QString &input) {
    const auto s = input.trimmed();
    if (s.isEmpty()) return {};
    static const QRegularExpression angleRe(QStringLiteral("<\\s*([^<>@\\s]+@[^<>@\\s]+)\\s*>"));
    if (const auto m = angleRe.match(s); m.hasMatch()) return m.captured(1).trimmed();
    static const QRegularExpression plainRe(
        QStringLiteral("\\b([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = plainRe.match(s); m.hasMatch()) return m.captured(1).trimmed();
    return {};
}

QString normalizeSenderValue(const QString &fromHeader, const QString &fallbackHeader) {
    auto from = cleanAngle(fromHeader).trimmed();
    auto email = extractEmailAddress(from);
    if (email.isEmpty()) email = extractEmailAddress(fallbackHeader);
    if (email.isEmpty()) return from;

    auto name = from;
    static const QRegularExpression angleBracketRe(QStringLiteral("<[^>]*>"));
    name.remove(angleBracketRe);
    name = name.trimmed();
    if ((name.startsWith('"') && name.endsWith('"')) ||
            (name.startsWith('\'') && name.endsWith('\'')))
        name = name.mid(1, name.size() - 2).trimmed();
    else {
        if (name.startsWith('"') || name.startsWith('\'')) name = name.mid(1).trimmed();
        if (name.endsWith('"') || name.endsWith('\'')) { name.chop(1); name = name.trimmed(); }
    }
    // Unescape RFC 2822 quoted-pair sequences (e.g. \" -> ") so that
    // names like Joseph "Jody" Hill render with their intended quotes.
    if (name.contains(QLatin1Char('\\'))) {
        QString unescaped;
        unescaped.reserve(name.size());
        bool esc = false;
        for (const QChar ch : std::as_const(name)) {
            if (esc) { unescaped.append(ch); esc = false; }
            else if (ch == QLatin1Char('\\')) { esc = true; }
            else { unescaped.append(ch); }
        }
        name = unescaped.trimmed();
    }
    if (name.isEmpty() || name.compare(email, Qt::CaseInsensitive) == 0) return email;
    return QStringLiteral("%1 <%2>").arg(name, email);
}

QString sanitizeAddressHeader(QString v) {
    static const QRegularExpression emptyRfc2047Re(QStringLiteral("=\\?[^?]+\\?[bBqQ]\\?\\?="));
    v.replace(emptyRfc2047Re, QString());
    v.replace(kWhitespaceRe, QStringLiteral(" "));
    return v.trimmed();
}

// ── Header decoding ───────────────────────────────────────────────────────────

QString decodeRfc2047(const QString &input) {
    auto out = input;
    qint32 guard = 0;
    while (guard++ < 32) {
        const auto m = kRfc2047Re.match(out);
        if (!m.hasMatch()) break;
        const auto encoding = m.captured(2);
        auto payload = m.captured(3);
        QByteArray bytes;
        if (encoding.compare(QStringLiteral("B"), Qt::CaseInsensitive) == 0) {
            bytes = QByteArray::fromBase64(payload.toUtf8());
        } else {
            payload.replace('_', ' ');
            auto qp = payload.toUtf8();
            qp.replace('=', '%');
            bytes = QUrl::fromPercentEncoding(qp).toUtf8();
        }
        out.replace(m.capturedStart(0), m.capturedLength(0), QString::fromUtf8(bytes));
    }
    return out;
}

// ── Snippet generation ────────────────────────────────────────────────────────

// Mirrors Mailspring's approach: extract body text via MIME parser, strip whitespace,
// truncate. No heuristic junk filters — correct extraction makes them unnecessary.
QString compileDeterministicSnippet(const QString &subject,
                                    const QString &headerSource,
                                    const QByteArray &fetchRespRaw)
{
    // 1. Check email-specific preheader header fields first
    static const QLatin1StringView preheaderFields[] = {
        "List-Preview"_L1, "X-Preheader"_L1, "X-MC-Preview-Text"_L1, "X-Alt-Description"_L1
    };
    for (const auto &field : preheaderFields) {
        const auto v = stripWhitespace(decodeRfc2047(
            Parser::extractField(headerSource, QString(field))));
        if (!v.isEmpty())
            return v.left(400);
    }

    // 2. Extract body text: plain text preferred, HTML flattened via flattenHtmlToText
    QString text = stripWhitespace(BodyProcessor::extractBodyTextForSnippet(fetchRespRaw));

    // 3. Strip zero-width / invisible characters
    text.removeIf([](const QChar c) {
        switch (c.unicode()) {
            case 0x034F: case 0x200B: case 0x200C: case 0x200D: case 0x2060:
            case 0xFEFF: case 0x00AD: case 0xFFFC: return true;
            default: return false;
        }
    });
    text = text.trimmed();

    // 4. Fall back to subject if body extraction yielded nothing
    if (text.isEmpty())
        text = stripWhitespace(subject);

    return text.left(400);
}

} // namespace Imap::MessageUtils
