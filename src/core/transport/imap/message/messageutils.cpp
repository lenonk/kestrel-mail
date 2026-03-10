#include "messageutils.h"

#include "../parser/responseparser.h"
#include "bodyprocessor.h"

#include <QRegularExpression>
#include <QUrl>

using namespace Qt::Literals::StringLiterals;

namespace Imap::MessageUtils {

namespace {
static const QRegularExpression kRfc2047Re(QStringLiteral("=\\?([^?]+)\\?([bBqQ])\\?([^?]+)\\?="));
static const QRegularExpression kAlphaRe(QStringLiteral("[A-Za-z]"));
static const QRegularExpression kHtmlStyleRe(QStringLiteral("<style[^>]*>[\\s\\S]*?</style>"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kHtmlScriptRe(QStringLiteral("<script[^>]*>[\\s\\S]*?</script>"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kHeaderRe(QStringLiteral("^\\s*[A-Za-z-]+:[^\\r\\n]*[\\r\\n]+"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kViewInBrowserRe(QStringLiteral("View (this email|email) in (your )?browser|Can't see images\\?|Trouble viewing"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kMarkdownLinkRe(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^)]+)\\)"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kUrlRe(QStringLiteral("https?://\\S+"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kQuotedPrintHexRe(QStringLiteral("(=[0-9A-Fa-f]{2})+"));
static const QRegularExpression kQuotedPrintSpaceRe(QStringLiteral("=09|=20"));
static const QRegularExpression kWhitespaceRe(QStringLiteral("\\s+"));
static const QRegularExpression kHtmlScriptStyleRe(QStringLiteral("<(script|style)[^>]*>[\\s\\S]*?</\\1>"), QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kHtmlTagRe(QStringLiteral("<[^>]+>"));
static const QRegularExpression kBase64TokenRe(QStringLiteral("[A-Za-z0-9+/=_-]{32,}"));
static const QRegularExpression kQuotedPrintSeqRe(QStringLiteral("(=[0-9A-Fa-f]{2}){1,}"));
static const QRegularExpression kSentenceSplitRe(QStringLiteral("(?<=[.!?])\\s+|\\s*[|•·]\\s*|\\s{2,}"));
static const QRegularExpression kHeaderLineRe(QStringLiteral("^[A-Za-z0-9-]+:\\s*.*$"));
static const QRegularExpression kBoundaryRe(QStringLiteral("^--[_=:+./?A-Za-z0-9-]{6,}.*$"));
static const QRegularExpression kImapTaggedRe("^s\\d+\\s+(OK|NO|BAD)\\b"_L1, QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kImapTrailerRe("\\)\\s*s\\d+\\s+(OK|NO|BAD)[^\\n]*"_L1, QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kCssOnsetRe(
    "\\}\\s*\\}|;\\s*\\}|/\\*|@(?:font-face|media|keyframes|import)\\b|\\{[ \\t]*(?:margin|padding|font|width|height|color|background|display|border|line-height|text-)"_L1,
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kCssPropLooseRe(
    "\\b(?:font-family|font-size|background-color|text-decoration|line-height|mso-|-webkit-|-ms-text|min-width|max-width|min-height|max-height|border-radius|box-shadow|vertical-align)\\s*:"_L1,
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kCssDimensionRe("\\b\\w[\\w-]*\\s*:\\s*\\d+(?:px|em|rem|vh|vw|pt)\\b"_L1,
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kLeadingLiteralNumRe("^\\d+\\s+(?=[<.#@])"_L1);
static const QRegularExpression kCssSelectorListRe(
    "\\b(?:table|td|tr|th|div|span|font|img|ul|ol|li|h[1-6]|section|header|footer|nav)\\s*,\\s*(?:table|td|tr|th|div|span|font|img|ul|ol|li|p|h[1-6]|section|header|footer|nav)\\b"_L1,
    QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kRepeatedSeparatorRe("(?:[-=_~]{5,}|(?:[-=*_~#][ \\t]){4,}[-=*_~#]?)"_L1);
static const QRegularExpression kImagePlaceholderRe("\\(\\s+\\)"_L1);
static const QRegularExpression kAsteriskDecoRe("\\*{2,}"_L1);

static QString htmlToPlainFast(QString s) {
    s.replace(kHtmlScriptStyleRe, " "_L1);
    s.replace(kHtmlTagRe, " "_L1);
    s.replace("&nbsp;"_L1, " "_L1, Qt::CaseInsensitive);
    s.replace("&amp;"_L1, "&"_L1, Qt::CaseInsensitive);
    s.replace("&lt;"_L1, "<"_L1, Qt::CaseInsensitive);
    s.replace("&gt;"_L1, ">"_L1, Qt::CaseInsensitive);
    s.replace("&#39;"_L1, "'"_L1, Qt::CaseInsensitive);
    s.replace("&quot;"_L1, "\""_L1, Qt::CaseInsensitive);
    return s;
}

static QString truncateCssJunk(const QString &text) {
    int pos = -1;
    const auto tryPos = [&](const QRegularExpression &re) {
        const auto m = re.match(text);
        if (m.hasMatch()) {
            const int p = m.capturedStart();
            if (pos < 0 || p < pos) pos = p;
        }
    };
    tryPos(kCssOnsetRe);
    tryPos(kCssPropLooseRe);
    tryPos(kCssDimensionRe);
    tryPos(kCssSelectorListRe);
    tryPos(kRepeatedSeparatorRe);
    if (pos <= 0) return text;
    return text.left(pos).trimmed();
}
}

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
    if (const auto m1 = angleRe.match(s); m1.hasMatch()) return m1.captured(1).trimmed();
    static const QRegularExpression plainRe(QStringLiteral("\\b([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})\\b"), QRegularExpression::CaseInsensitiveOption);
    if (const auto m2 = plainRe.match(s); m2.hasMatch()) return m2.captured(1).trimmed();
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

    if ((name.startsWith('"') && name.endsWith('"')) || (name.startsWith('\'') && name.endsWith('\''))) {
        name = name.mid(1, name.size() - 2).trimmed();
    } else {
        if (name.startsWith('"') || name.startsWith('\'')) name = name.mid(1).trimmed();
        if (name.endsWith('"') || name.endsWith('\'')) { name.chop(1); name = name.trimmed(); }
    }

    if (name.isEmpty() || name.compare(email, Qt::CaseInsensitive) == 0) return email;
    return QStringLiteral("%1 <%2>").arg(name, email);
}

QString sanitizeAddressHeader(QString v) {
    static const QRegularExpression emptyRfc2047Re(QStringLiteral("=\\?[^?]+\\?[bBqQ]\\?\\?="));
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));
    v.replace(emptyRfc2047Re, QString());
    v.replace(whitespaceRe, QStringLiteral(" "));
    return v.trimmed();
}

QString decodeRfc2047(const QString &input) {
    auto out = input; qint32 guard = 0;
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
            auto qp = payload.toUtf8(); qp.replace('=', '%');
            bytes = QUrl::fromPercentEncoding(qp).toUtf8();
        }
        out.replace(m.capturedStart(0), m.capturedLength(0), QString::fromUtf8(bytes));
    }
    return out;
}

qint32 snippetQualityScore(const QString &snippet) {
    const auto t = snippet.toLower(); qint32 score = 0;
    if (snippet.size() >= 20 && snippet.size() <= 160) score += 6;
    score += static_cast<qint32>(qMin(8, snippet.count(' ')));
    if (t.contains("url=") || t.contains("utm_") || t.contains("content-type") || t.contains("charset=") || t.contains("mi_credit") || t.contains("http")) score -= 10;
    if (t.contains("( )") || t.contains("{margin") || t.contains("font-family")) score -= 8;
    const auto openBraces = snippet.count('{');
    if (const auto closeBraces = snippet.count('}'); openBraces + closeBraces >= 3) score -= 40;
    if (snippet.count(kAlphaRe) > 12) score += 4;
    return score;
}

bool snippetLooksLikeProtocolOrJunk(const QString &in) {
    const auto s = in.trimmed().toLower();
    if (s.isEmpty()) return true;
    if (s.startsWith(QStringLiteral("* "))) return true;
    if (kImapTaggedRe.match(s).hasMatch()) return true;
    static const QStringList bad = {
        QStringLiteral("content-type"), QStringLiteral("content-transfer-encoding"), QStringLiteral("body[header.fields"),
        QStringLiteral("x-gm-labels"), QStringLiteral("throttled"), QStringLiteral("can't see images"),
        QStringLiteral("trouble viewing"), QStringLiteral("view this email in your browser"), QStringLiteral("view as a web page"),
        QStringLiteral("view in browser"), QStringLiteral("unsubscribe"), QStringLiteral("forward this email"),
        QStringLiteral("if you no longer wish"), QStringLiteral("privacy policy"), QStringLiteral("doctype html"),
        QStringLiteral("<link rel"), QStringLiteral("<style"), QStringLiteral("xmlns="), QStringLiteral(" table {"),
        QStringLiteral(" img {"), "font-family:"_L1, "@font-face"_L1, "@media"_L1, "!important"_L1,
        "-webkit-"_L1, "[class="_L1, "@import"_L1, ".mj-"_L1, "mi_credit"_L1, "actioncode="_L1, "utm_"_L1,
    };
    for (const auto &k : bad) if (s.contains(k)) return true;
    if (s.contains(QChar(0x034F)) || s.contains(QChar(0x200C))) return true;
    if (s.count('{') + s.count('}') >= 3) return true;
    return false;
}

bool snippetLooksGarbledBytes(const QString &s) {
    if (s.isEmpty()) return true;
    int ascii=0, printable=0, weird=0;
    for (const QChar ch : s) {
        if (ch.isSpace() || ch.isLetterOrNumber()) { ++printable; if (ch.unicode() < 128) ++ascii; continue; }
        if (QStringLiteral(".,;:!?()[]{}'\"-_/&%$#@+*=|~`<>").contains(ch)) { ++printable; if (ch.unicode() < 128) ++ascii; }
        else ++weird;
    }
    if (printable < qMax(12, s.size()/4)) return true;
    if (weird > s.size()/5) return true;
    if (s.size() >= 24 && ascii < s.size()/6) return true;
    return false;
}

QString stripUnsafeUnicodeForSnippet(const QString &in) {
    QString out; out.reserve(in.size());
    for (const auto &ch : in) {
        const auto cat = ch.category();
        if (cat == QChar::Other_Control || cat == QChar::Other_Format || cat == QChar::Other_Surrogate || cat == QChar::Other_PrivateUse || cat == QChar::Other_NotAssigned)
            continue;
        out.append(ch);
    }
    return out;
}

QString cleanSnippet(const QString &raw) {
    QString s = stripUnsafeUnicodeForSnippet(raw);
    s.replace(kHtmlStyleRe, QString()); s.replace(kHtmlScriptRe, QString());
    while (kHeaderRe.match(s).hasMatch()) s.replace(kHeaderRe, QString());
    s.remove(QChar(0x200B)); s.remove(QChar(0x200C)); s.remove(QChar(0x200D)); s.remove(QChar(0x2060));
    s.remove(QChar(0xFEFF)); s.remove(QChar(0x00AD)); s.remove(QChar(0x034F)); s.remove(QChar(0xFFFC));
    s.replace(QChar(0x2007), QLatin1Char(' ')); s.replace(QChar(0x00A0), QLatin1Char(' '));
    s.replace(kViewInBrowserRe, QString()); s.replace(kMarkdownLinkRe, QStringLiteral("\\1")); s.replace(kUrlRe, QStringLiteral(" "));
    s.replace(kQuotedPrintHexRe, QStringLiteral(" ")); s.replace(kQuotedPrintSpaceRe, QStringLiteral(" ")); s.replace(kWhitespaceRe, QStringLiteral(" "));
    s = s.trimmed(); if (s.size() > 140) s = s.left(140); return s;
}

QString extractReadableFallbackForSnippet(const QString &raw) {
    auto s = decodeRfc2047(raw);
    s.replace(kHtmlScriptStyleRe, QStringLiteral(" ")); s.replace(kHtmlTagRe, QStringLiteral(" ")); s.replace(kUrlRe, QStringLiteral(" "));
    s.replace(kBase64TokenRe, QStringLiteral(" ")); s.replace(kQuotedPrintSeqRe, QStringLiteral(" ")); s.replace(QStringLiteral("=20"), QStringLiteral(" "));
    s.replace(kWhitespaceRe, QStringLiteral(" ")); s = s.trimmed();
    const auto parts = s.split(kSentenceSplitRe, Qt::SkipEmptyParts);
    QString best; int bestScore = -99999;
    for (auto p : parts) {
        p = p.trimmed(); if (p.size() < 12) continue;
        const auto pl = p.toLower(); if (pl.count(kViewInBrowserRe)) continue;
        if (pl.startsWith(QStringLiteral("unsubscribe")) || pl.contains(QStringLiteral("privacy policy"))) continue;
        if (p.contains('<') || p.contains('>')) continue;
        if (snippetLooksLikeProtocolOrJunk(p)) continue;
        auto words = p.split(kWhitespaceRe, Qt::SkipEmptyParts).size(); const auto alpha = p.count(kAlphaRe);
        if (alpha < 8 || words < 3) continue;
        auto score = qMin(12, words) + qMin(10, alpha/6);
        if (pl.contains(QStringLiteral("order"))) score += 4;
        if (pl.contains(QStringLiteral("account")) || pl.contains(QStringLiteral("access"))) score += 3;
        if (score > bestScore) { best = p; bestScore = score; }
    }
    return best.left(180);
}

QString stripWhitespaceForSnippet(const QString &in) {
    QString out; out.reserve(in.size());
    for (const auto ch : in) {
        const auto u = ch.unicode();
        if (u=='\t' || u=='\n' || u=='\f' || u=='\r' || u==160 || u==133 || u==0x2028 || u==0x2029) out.append(' ');
        else out.append(ch);
    }
    out = out.trimmed(); QString collapsed; collapsed.reserve(out.size()); bool prevSpace=false;
    for (const auto ch : out) { const auto isSpace = (ch==' '); if (isSpace && prevSpace) continue; collapsed.append(ch); prevSpace=isSpace; }
    return collapsed.trimmed();
}

QString stripMimeNoiseForSnippet(QString text) {
    text.replace("\r\n", "\n"); text.replace('\r', '\n');
    const QStringList lines = text.split('\n'); QStringList kept; kept.reserve(lines.size()); bool inPartHeaders=false;
    for (const auto &line : lines) {
        const auto t = line.trimmed();
        if (t.isEmpty()) { if (inPartHeaders) inPartHeaders=false; else if (!kept.isEmpty()) kept.push_back(QString()); continue; }
        if (kBoundaryRe.match(t).hasMatch()) { inPartHeaders=true; continue; }
        const auto lower = t.toLower();
        if (lower == QStringLiteral("this is a multi-part message in mime format.")) continue;
        if (lower.startsWith(QStringLiteral("content-type:")) || lower.startsWith(QStringLiteral("content-transfer-encoding:")) || lower.startsWith(QStringLiteral("content-disposition:")) || lower.startsWith(QStringLiteral("content-id:")) || lower.startsWith(QStringLiteral("mime-version:"))) { inPartHeaders=true; continue; }
        if (inPartHeaders && kHeaderLineRe.match(t).hasMatch()) continue;
        kept.push_back(line);
    }
    return kept.join(' ');
}

QString compileDeterministicSnippet(const QString &subject, const QString &headerSource, const QByteArray &fetchRespRaw) {
    const QStringList preheaderFields = { QStringLiteral("List-Preview"), QStringLiteral("X-Preheader"), QStringLiteral("X-MC-Preview-Text"), QStringLiteral("X-Alt-Description") };
    QString text;
    for (const QString &field : preheaderFields) {
        text = stripWhitespaceForSnippet(decodeRfc2047(Parser::extractField(headerSource, field)).trimmed());
        if (!text.isEmpty()) break;
    }
    if (text.isEmpty()) {
        if (const auto html = BodyProcessor::extractBodyHtmlFromFetch(fetchRespRaw).trimmed(); !html.isEmpty()) {
            text = BodyProcessor::extractHiddenPreheader(html);
            if (!text.isEmpty()) text = stripWhitespaceForSnippet(text);
            else {
                text = htmlToPlainFast(html);
                text.replace(kImagePlaceholderRe, " "_L1); text.replace(kAsteriskDecoRe, " "_L1);
                text = stripWhitespaceForSnippet(text); text = truncateCssJunk(text);
            }
        }
    }
    if (text.isEmpty()) {
        if (const auto literal = Parser::extractLastLiteralBytesFromFetch(fetchRespRaw); !literal.isEmpty()) {
            const auto decoded = BodyProcessor::decodeTransferEncoded(literal);
            auto literalText = QString::fromUtf8(decoded);
            literalText.replace(kImapTrailerRe, QString()); literalText.replace(kLeadingLiteralNumRe, QString());
            auto bodyStart = literalText.indexOf(QStringLiteral("\r\n\r\n")); int sepLen=4;
            if (bodyStart < 0) { bodyStart = literalText.indexOf(QStringLiteral("\n\n")); sepLen = 2; }
            if (bodyStart > 0) {
                const auto lead = literalText.left(bodyStart).toLower();
                if (lead.contains(QStringLiteral("delivered-to:")) || lead.contains(QStringLiteral("received:")) || lead.contains(QStringLiteral("return-path:")) || lead.contains(QStringLiteral("message-id:")) || lead.contains(QStringLiteral("mime-version:")))
                    literalText = literalText.mid(bodyStart + sepLen);
            }
            literalText = stripMimeNoiseForSnippet(literalText);
            if (literalText.contains('<'))
                literalText = htmlToPlainFast(literalText);
            literalText.replace(kImagePlaceholderRe, " "_L1); literalText.replace(kAsteriskDecoRe, " "_L1);
            text = stripWhitespaceForSnippet(literalText); text = truncateCssJunk(text);
        }
    }
    if (text.isEmpty()) {
        if (const auto literal = Parser::extractLastLiteralBytesFromFetch(fetchRespRaw); !literal.isEmpty()) {
            auto fallback = QString::fromUtf8(BodyProcessor::decodeTransferEncoded(literal));
            fallback = extractReadableFallbackForSnippet(fallback); fallback = stripWhitespaceForSnippet(fallback);
            if (!fallback.isEmpty()) text = fallback;
        }
    }
    if (text.isEmpty()) {
        auto bodyFallback = BodyProcessor::extractBodySnippetFromFetch(fetchRespRaw);
        bodyFallback = cleanSnippet(bodyFallback); bodyFallback = stripWhitespaceForSnippet(bodyFallback);
        if (!bodyFallback.isEmpty()) text = bodyFallback;
    }
    text = stripWhitespaceForSnippet(text);
    text.removeIf([] (const QChar c) {
        switch (c.unicode()) {
            case 0x034F: case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF: case 0x00AD: case 0xFFFC:
                return true;
            default: break;
        }
        return false;
    });
    text = stripWhitespaceForSnippet(text);
    if (const auto t = text.toLower(); t.contains(QStringLiteral("content-type:")) || t.contains(QStringLiteral("content-transfer-encoding:")) || t.contains(QStringLiteral("mime-version:")) || t.contains("no text content"_L1)) text.clear();
    else if (snippetLooksGarbledBytes(text)) text.clear();
    else if (text.count('{') + text.count('}') >= 3) text.clear();
    else if (kRepeatedSeparatorRe.match(text).hasMatch() && text.count(kAlphaRe) < 10) text.clear();
    else if (text.size() > 50 && text.count(' ') * 8 < text.size()) text.clear();
    else text = truncateCssJunk(text);
    if (text.isEmpty()) text = stripWhitespaceForSnippet(subject);
    return text.left(140);
}

} // namespace Imap::MessageUtils
