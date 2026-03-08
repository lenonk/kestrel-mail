#include "snippetutils.h"
#include "../parser/responseparser.h"
#include "bodyprocessor.h"
#include <QRegularExpression>
#include <QTextDocument>

using namespace Qt::Literals::StringLiterals;

namespace Imap::SnippetUtils {

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

    // IMAP protocol leak detection
    // Matches tagged IMAP responses at the start: "s124 OK Success"
    static const QRegularExpression kImapTaggedRe("^s\\d+\\s+(OK|NO|BAD)\\b"_L1, QRegularExpression::CaseInsensitiveOption);
    // Matches IMAP FETCH close + tagged response anywhere: ") s138 OK Success"
    static const QRegularExpression kImapTrailerRe("\\)\\s*s\\d+\\s+(OK|NO|BAD)[^\\n]*"_L1, QRegularExpression::CaseInsensitiveOption);

    // CSS junk onset: } }, ; }, /* comments, @rules, { property-name
    static const QRegularExpression kCssOnsetRe(
        "\\}\\s*\\}|;\\s*\\}|/\\*|@(?:font-face|media|keyframes|import)\\b"
        "|\\{[ \\t]*(?:margin|padding|font|width|height|color|background|display|border|line-height|text-)"_L1,
        QRegularExpression::CaseInsensitiveOption);
    // Inline CSS properties (unambiguous ones that never appear in normal prose)
    static const QRegularExpression kCssPropLooseRe(
        "\\b(?:font-family|font-size|background-color|text-decoration|line-height|mso-|"
        "-webkit-|-ms-text|min-width|max-width|min-height|max-height|border-radius|"
        "box-shadow|vertical-align)\\s*:"_L1,
        QRegularExpression::CaseInsensitiveOption);
    // CSS dimension values: "width: 100px", "height: 50%", "padding: 0 auto"
    // Number followed by a CSS unit strongly implies a CSS property declaration.
    static const QRegularExpression kCssDimensionRe(
        "\\b\\w[\\w-]*\\s*:\\s*\\d+(?:px|em|rem|vh|vw|pt)\\b"_L1,
        QRegularExpression::CaseInsensitiveOption);
    // IMAP literal-size number leaked before HTML/CSS: "96 <link href=" or "96 .mj-class"
    static const QRegularExpression kLeadingLiteralNumRe("^\\d+\\s+(?=[<.#@])"_L1);

    // CSS element selector lists: "table, td, tr, div, font, p, li"
    // Two or more HTML element names separated by commas is unambiguously CSS.
    static const QRegularExpression kCssSelectorListRe(
        "\\b(?:table|td|tr|th|div|span|font|img|ul|ol|li|h[1-6]|section|header|footer|nav)\\s*,"
        "\\s*(?:table|td|tr|th|div|span|font|img|ul|ol|li|p|h[1-6]|section|header|footer|nav)\\b"_L1,
        QRegularExpression::CaseInsensitiveOption);

    // Repeated visual separator runs: "- - - - -", "*****", "====="
    // 5+ consecutive dashes/equals/underscores/tildes OR 4+ (separator + space) repetitions.
    // Intentionally does NOT match "--" (email signature) or "---" (Markdown).
    static const QRegularExpression kRepeatedSeparatorRe(
        "(?:[-=_~]{5,}|(?:[-=*_~#][ \\t]){4,}[-=*_~#]?)"_L1);

    // QTextDocument image/object placeholder produced by <img> and similar elements.
    static const QRegularExpression kImagePlaceholderRe("\\(\\s+\\)"_L1);

    // Asterisk decoration runs used as visual dividers in marketing emails: "***"
    static const QRegularExpression kAsteriskDecoRe("\\*{2,}"_L1);
}

QString
decodeRfc2047(const QString &input) {
    auto out = input;
    qint32 guard = 0;

    while (guard++ < 32) {
        const auto m = kRfc2047Re.match(out);
        if (!m.hasMatch()) { break; };

        const auto encoding = m.captured(2);
        auto payload = m.captured(3);
        QByteArray bytes;

        if (encoding.compare(QStringLiteral("B"), Qt::CaseInsensitive) == 0) {
            bytes = QByteArray::fromBase64(payload.toUtf8());
        }
        else {
            payload.replace('_', ' ');
            auto qp = payload.toUtf8();
            qp.replace('=', '%');
            bytes = QUrl::fromPercentEncoding(qp).toUtf8();
        }

        QString decoded = QString::fromUtf8(bytes);

        out.replace(m.capturedStart(0), m.capturedLength(0), decoded);
    }
    return out;
}

qint32
snippetQualityScore(const QString &snippet) {
    const auto t = snippet.toLower();
    qint32 score = 0;

    if (snippet.size() >= 20 && snippet.size() <= 160)
        score += 6;

    score += static_cast<qint32>(qMin(8, snippet.count(' ')));

    if (t.contains("url=") || t.contains("utm_") || t.contains("content-type") ||
        t.contains("charset=") || t.contains("mi_credit") || t.contains("http")) {
        score -= 10;
    }

    if (t.contains("( )") || t.contains("{margin") || t.contains("font-family")) {
        score -= 8;
    }

    const auto openBraces = snippet.count(QLatin1Char('{'));

    if (const auto closeBraces = snippet.count(QLatin1Char('}')); openBraces + closeBraces >= 3)
        score -= 40;  // aggressive CSS penalty

    if (snippet.count(kAlphaRe) > 12)
        score += 4;

    return score;
}

bool
snippetLooksLikeProtocolOrJunk(const QString &in) {
    const auto s = in.trimmed().toLower();

    if (s.isEmpty())
        return true;

    if (s.startsWith(QStringLiteral("* ")))
        return true;

    // IMAP tagged response: "s124 OK Success"
    if (kImapTaggedRe.match(s).hasMatch())
        return true;

    static const QStringList bad = {
        QStringLiteral("content-type"), QStringLiteral("content-transfer-encoding"),
        QStringLiteral("body[header.fields"), QStringLiteral("x-gm-labels"), QStringLiteral("throttled"),
        QStringLiteral("can't see images"), QStringLiteral("trouble viewing"),
        QStringLiteral("view this email in your browser"), QStringLiteral("view as a web page"),
        QStringLiteral("view in browser"), QStringLiteral("unsubscribe"),
        QStringLiteral("forward this email"), QStringLiteral("if you no longer wish"),
        QStringLiteral("privacy policy"), QStringLiteral("doctype html"),
        QStringLiteral("<link rel"), QStringLiteral("<style"), QStringLiteral("xmlns="),
        QStringLiteral(" table {"), QStringLiteral(" img {"),
        // CSS indicators
        "font-family:"_L1, "@font-face"_L1, "@media"_L1, "!important"_L1,
        "-webkit-"_L1, "[class="_L1, "@import"_L1,
        ".mj-"_L1,  // Mailjet CSS class prefix
        // URL tracking parameters
        "mi_credit"_L1, "actioncode="_L1, "utm_"_L1,
    };

    for (const auto &k : bad) {
        if (s.contains(k))
            return true;
    }

    if (s.contains(QChar(0x034F)) || s.contains(QChar(0x200C)))
        return true;

    // Too many CSS braces — likely a CSS rule block
    if (s.count('{') + s.count('}') >= 3)
        return true;

    return false;
}

bool
snippetLooksGarbledBytes(const QString &s) {
    if (s.isEmpty())
        return true;

    int ascii = 0;
    int printable = 0;
    int weird = 0;

    for (const QChar ch : s) {
        if (ch.isSpace() || ch.isLetterOrNumber()) {
            ++printable;
            if (ch.unicode() < 128)
                ++ascii;
            continue;
        }

        if (QStringLiteral(".,;:!?()[]{}'\"-_/&%$#@+*=|~`<>").contains(ch)) {
            ++printable;
            if (ch.unicode() < 128)
                ++ascii;
        }
        else {
            ++weird;
        }
    }

    if (printable < qMax(12, s.size() / 4))
        return true;

    if (weird > s.size() / 5)
        return true;

    if (s.size() >= 24 && ascii < s.size() / 6)
        return true;

    return false;
}

QString
stripUnsafeUnicodeForSnippet(const QString &in) {
    QString out;

    out.reserve(in.size());

    for (const auto &ch : in) {
        const auto cat = ch.category();
        if (cat == QChar::Other_Control || cat == QChar::Other_Format || cat == QChar::Other_Surrogate
            || cat == QChar::Other_PrivateUse || cat == QChar::Other_NotAssigned) {
            continue;
        }

        out.append(ch);
    }

    return out;
}

QString
cleanSnippet(const QString &raw) {
    QString s = stripUnsafeUnicodeForSnippet(raw);

    // Remove style/script blocks early if HTML leaked into this stage.
    s.replace(kHtmlStyleRe, QString());
    s.replace(kHtmlScriptRe, QString());

    // 1) Aggressive multi-line header strip (loop until stable)
    while (kHeaderRe.match(s).hasMatch()) {
        s.replace(kHeaderRe, QString());
    }

    // 2) Zero-width/control junk
    s.remove(QChar(0x200B));
    s.remove(QChar(0x200C));
    s.remove(QChar(0x200D));
    s.remove(QChar(0x2060));
    s.remove(QChar(0xFEFF));
    s.remove(QChar(0x00AD));
    s.remove(QChar(0x034F)); // combining grapheme joiner seen in noisy promos
    s.remove(QChar(0xFFFC)); // object replacement char from HTML/image placeholders
    s.replace(QChar(0x2007), QLatin1Char(' '));
    s.replace(QChar(0x00A0), QLatin1Char(' '));

    // 3) Common newsletter openers
    s.replace(kViewInBrowserRe, QString());

    // Convert markdown-style links to visible label, then drop bare links.
    s.replace(kMarkdownLinkRe, QStringLiteral("\\1"));
    s.replace(kUrlRe, QStringLiteral(" "));

    s.replace(kQuotedPrintHexRe, QStringLiteral(" "));
    s.replace(kQuotedPrintSpaceRe, QStringLiteral(" "));
    s.replace(kWhitespaceRe, QStringLiteral(" "));

    s = s.trimmed();

    if (s.size() > 140)
        s = s.left(140);

    return s;
}

QString
extractReadableFallbackForSnippet(const QString &raw) {
    auto s = decodeRfc2047(raw);

    s.replace(kHtmlScriptStyleRe, QStringLiteral(" "));
    s.replace(kHtmlTagRe, QStringLiteral(" "));
    s.replace(kUrlRe, QStringLiteral(" "));
    s.replace(kBase64TokenRe, QStringLiteral(" "));
    s.replace(kQuotedPrintSeqRe, QStringLiteral(" "));
    s.replace(QStringLiteral("=20"), QStringLiteral(" "));
    s.replace(kWhitespaceRe, QStringLiteral(" "));
    s = s.trimmed();

    const auto parts = s.split(kSentenceSplitRe, Qt::SkipEmptyParts);

    QString best;
    int bestScore = -99999;

    for (auto p : parts) {
        p = p.trimmed();

        if (p.size() < 12)
            continue;

        const auto pl = p.toLower();
        // if (pl.startsWith(QStringLiteral("view in browser")) || pl.startsWith(QStringLiteral("view in your browser")))
        if (pl.count(kViewInBrowserRe))
            continue;

        if (pl.startsWith(QStringLiteral("unsubscribe")) || pl.contains(QStringLiteral("privacy policy")))
            continue;

        if (p.contains('<') || p.contains('>'))
            continue;

        if (snippetLooksLikeProtocolOrJunk(p))
            continue;

        auto words = p.split(kWhitespaceRe, Qt::SkipEmptyParts).size();
        const auto alpha = p.count(kAlphaRe);

        if (alpha < 8 || words < 3)
            continue;

        auto score = qMin(12, words) + qMin(10, alpha / 6);
        if (pl.contains(QStringLiteral("order")))
            score += 4;

        if (pl.contains(QStringLiteral("account")) || pl.contains(QStringLiteral("access")))
            score += 3;

        if (score > bestScore) {
            best = p;
            bestScore = score;
        }
    }

    return best.left(180);
}

QString
stripWhitespaceForSnippet(const QString &in) {
    QString out;

    out.reserve(in.size());

    for (const auto ch : in) {
        const auto u = ch.unicode();
        if (u == '\t' || u == '\n' || u == '\f' || u == '\r' || u == 160 || u == 133 || u == 0x2028 || u == 0x2029) {
            out.append(QLatin1Char(' '));
        }
        else {
            out.append(ch);
        }
    }

    out = out.trimmed();

    QString collapsed;
    collapsed.reserve(out.size());
    bool prevSpace = false;

    for (const auto ch : out) {
        const auto isSpace = (ch == QLatin1Char(' '));

        if (isSpace && prevSpace)
            continue;

        collapsed.append(ch);
        prevSpace = isSpace;
    }

    return collapsed.trimmed();
}

QString
stripMimeNoiseForSnippet(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    const QStringList lines = text.split('\n');
    QStringList kept;
    kept.reserve(lines.size());

    bool inPartHeaders = false;

    for (const auto &line : lines) {
        const auto t = line.trimmed();
        if (t.isEmpty()) {
            if (inPartHeaders) {
                inPartHeaders = false;
            }
            else if (!kept.isEmpty()) {
                kept.push_back(QString());
            }

            continue;
        }

        if (kBoundaryRe.match(t).hasMatch()) {
            inPartHeaders = true;
            continue;
        }

        const auto lower = t.toLower();
        if (lower == QStringLiteral("this is a multi-part message in mime format."))
            continue;

        if (lower.startsWith(QStringLiteral("content-type:"))
            || lower.startsWith(QStringLiteral("content-transfer-encoding:"))
            || lower.startsWith(QStringLiteral("content-disposition:"))
            || lower.startsWith(QStringLiteral("content-id:"))
            || lower.startsWith(QStringLiteral("mime-version:"))) {
            inPartHeaders = true;

            continue;
        }

        if (inPartHeaders && kHeaderLineRe.match(t).hasMatch()) {
            continue;
        }

        kept.push_back(line);
    }

    return kept.join(' ');
}

// Find the earliest position where CSS/protocol junk begins and truncate there.
// Returns the text unchanged if no junk boundary is found.
static QString
truncateCssJunk(const QString &text) {
    int pos = -1;
    const auto tryPos = [&](const QRegularExpression &re) {
        const auto m = re.match(text);
        if (m.hasMatch()) {
            const int p = m.capturedStart();
            if (pos < 0 || p < pos)
                pos = p;
        }
    };
    tryPos(kCssOnsetRe);
    tryPos(kCssPropLooseRe);
    tryPos(kCssDimensionRe);
    tryPos(kCssSelectorListRe);
    tryPos(kRepeatedSeparatorRe);
    if (pos <= 0)
        return text;
    return text.left(pos).trimmed();
}

QString
compileDeterministicSnippet(const QString &subject,
                            const QString &headerSource,
                            const QByteArray &fetchRespRaw) {

    const QStringList preheaderFields = {
        QStringLiteral("List-Preview"),
        QStringLiteral("X-Preheader"),
        QStringLiteral("X-MC-Preview-Text"),
        QStringLiteral("X-Alt-Description")
    };

    QString text;
    for (const QString &field : preheaderFields) {
        text = stripWhitespaceForSnippet(decodeRfc2047(Parser::extractField(headerSource, field)).trimmed());
        if (!text.isEmpty()) {
            break;
        }
    }

    if (text.isEmpty()) {
        if (const auto html = BodyProcessor::extractBodyHtmlFromFetch(fetchRespRaw).trimmed(); !html.isEmpty()) {
            // Check for hidden preheader text first (display:none preview spans in newsletters).
            text = BodyProcessor::extractHiddenPreheader(html);
            if (!text.isEmpty()) {
                text = stripWhitespaceForSnippet(text);
            } else {
                QTextDocument doc;
                doc.setHtml(html);
                text = doc.toPlainText();
                text.replace(kImagePlaceholderRe, " "_L1);
                text.replace(kAsteriskDecoRe, " "_L1);
                text = stripWhitespaceForSnippet(text);
                text = truncateCssJunk(text);
            }
        }
    }

    if (text.isEmpty()) {
        if (const auto literal = Parser::extractLastLiteralBytesFromFetch(fetchRespRaw); !literal.isEmpty()) {
            const auto decoded = BodyProcessor::decodeTransferEncoded(literal);
            auto literalText = QString::fromUtf8(decoded);

            // Strip IMAP FETCH close + tagged response that sometimes leaks in
            // e.g. ") s138 OK Success" appended after literal bytes
            literalText.replace(kImapTrailerRe, QString());

            // Strip IMAP literal-size number that leaked before HTML content
            // e.g. "96 <link href=..." where 96 is the {size} marker value
            literalText.replace(kLeadingLiteralNumRe, QString());

            auto bodyStart = literalText.indexOf(QStringLiteral("\r\n\r\n"));
            int sepLen = 4;

            if (bodyStart < 0) {
                bodyStart = literalText.indexOf(QStringLiteral("\n\n"));
                sepLen = 2;
            }

            if (bodyStart > 0) {
                const auto lead = literalText.left(bodyStart).toLower();
                if (lead.contains(QStringLiteral("delivered-to:"))
                        || lead.contains(QStringLiteral("received:"))
                        || lead.contains(QStringLiteral("return-path:"))
                        || lead.contains(QStringLiteral("message-id:"))
                        || lead.contains(QStringLiteral("mime-version:"))) {
                    literalText = literalText.mid(bodyStart + sepLen);
                }
            }

            literalText = stripMimeNoiseForSnippet(literalText);
            if (literalText.contains('<')) {
                QTextDocument doc;
                doc.setHtml(literalText);
                literalText = doc.toPlainText();
            }
            literalText.replace(kImagePlaceholderRe, " "_L1);
            literalText.replace(kAsteriskDecoRe, " "_L1);
            text = stripWhitespaceForSnippet(literalText);
            text = truncateCssJunk(text);
        }
    }

    if (text.isEmpty()) {
        if (const auto literal = Parser::extractLastLiteralBytesFromFetch(fetchRespRaw); !literal.isEmpty()) {
            auto fallback = QString::fromUtf8(BodyProcessor::decodeTransferEncoded(literal));
            fallback = extractReadableFallbackForSnippet(fallback);
            fallback = stripWhitespaceForSnippet(fallback);
            if (!fallback.isEmpty())
                text = fallback;
        }
    }

    if (text.isEmpty()) {
        auto bodyFallback = BodyProcessor::extractBodySnippetFromFetch(fetchRespRaw);
        bodyFallback = cleanSnippet(bodyFallback);
        bodyFallback = stripWhitespaceForSnippet(bodyFallback);
        if (!bodyFallback.isEmpty())
            text = bodyFallback;
    }

    text = stripWhitespaceForSnippet(text);

    text.removeIf([] (const QChar c) {
        switch (c.unicode()) {
            case 0x034F:
            case 0x200B:
            case 0x200C:
            case 0x200D:
            case 0x2060:
            case 0xFEFF:
            case 0x00AD:
            case 0xFFFC:  // object replacement char (QTextDocument image placeholder)
                return true;
            default: break;
        }

        return false;
    });

    text = stripWhitespaceForSnippet(text);

    if (const auto t = text.toLower();
        t.contains(QStringLiteral("content-type:")) ||
        t.contains(QStringLiteral("content-transfer-encoding:")) ||
        t.contains(QStringLiteral("mime-version:")) ||
        t.contains("no text content"_L1)) {
        text.clear();
    }
    else if (snippetLooksGarbledBytes(text)) {
        text.clear();
    }
    // Reject CSS/junk that survived all earlier filters
    else if (text.count('{') + text.count('}') >= 3) {
        text.clear();
    }
    // Reject pure separator runs ("- - - - - -", "=====") with no real words
    else if (kRepeatedSeparatorRe.match(text).hasMatch() && text.count(kAlphaRe) < 10) {
        text.clear();
    }
    // Reject dense encoded blobs (base64, binary) — very few word breaks
    else if (text.size() > 50 && text.count(' ') * 8 < text.size()) {
        text.clear();
    }
    // One last CSS truncation pass on whatever made it through
    else {
        text = truncateCssJunk(text);
    }

    if (text.isEmpty()) {
        text = stripWhitespaceForSnippet(subject);
    }

    return text.left(140);
}

}
