#include "responseparser.h"
#include <QRegularExpression>
#include <QDateTime>
#include <QLocale>
#include <QMutex>
#include <QHash>

using namespace Qt::Literals::StringLiterals;

namespace Imap::Parser {

// REs are static const so that they only get compiled once, avoiding a large performance hit

QStringList
parseUidSearchAll(const QString &resp) {
    static const QRegularExpression eolRe("\\r?\\n"_L1);
    static const QRegularExpression spacesRe("\\s+"_L1);

    // ReSharper disable once CppTooWideScopeInitStatement
    const auto lines = resp.split(eolRe, Qt::SkipEmptyParts);
    for (const auto &line : lines) {
        const auto t = line.trimmed();
        if (!t.startsWith("* SEARCH"_L1, Qt::CaseInsensitive))
            continue;

        const auto tail = t.mid("* SEARCH"_L1.size()).trimmed();
        if (tail.isEmpty())
            return {};

        return tail.split(spacesRe, Qt::SkipEmptyParts);
    }

    return {};
}

QStringList
parseSearchIds(const QString &searchResp) {
    QStringList out;

    static const QRegularExpression eolRe("\\r?\\n"_L1);
    static const QRegularExpression searchRe("^\\*\\s+SEARCH\\s*(.*)$"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression spaceRe("\\s+"_L1);

    for (const auto &lines = searchResp.split(eolRe, Qt::SkipEmptyParts); const auto &line : lines) {
        const auto m = searchRe.match(line.trimmed());
        if (!m.hasMatch())
            continue;

        const auto payload = m.captured(1).trimmed();
        if (payload.isEmpty())
            continue;

        for (const auto &parts = payload.split(spaceRe, Qt::SkipEmptyParts); const auto &p : parts) {
            auto ok = false;
            p.toLongLong(&ok);
            if (ok)
                out << p;
        }
    }
    return out;
}

QString
extractField(const QString &input, const QString &field) {
    // Cache compiled regexes per field name. The static regex pattern cannot be
    // parameterised — using a single `static const QRegularExpression` here would
    // capture the first field name forever and return wrong results for all others.
    static QMutex s_cacheMutex;
    static QHash<QString, QRegularExpression> s_reCache;

    QRegularExpression re;
    {
        QMutexLocker locker(&s_cacheMutex);
        auto it = s_reCache.find(field);
        if (it == s_reCache.end()) {
            // Capture the first line plus any RFC 5322 continuation lines
            // (lines starting with whitespace).
            it = s_reCache.insert(field,
                QRegularExpression(
                    "(?:^|\\r?\\n)%1:\\s*([^\\r\\n]+(?:\\r?\\n[ \\t]+[^\\r\\n]+)*)"_L1
                        .arg(QRegularExpression::escape(field)),
                    QRegularExpression::CaseInsensitiveOption));
        }
        re = *it;
    }

    const auto m = re.match(input);
    if (!m.hasMatch()) return {};

    // Unfold RFC 5322 continuation lines into a single line.
    static const QRegularExpression foldRe("\\r?\\n[ \\t]+"_L1);
    return m.captured(1).replace(foldRe, " "_L1).trimmed();
}

QString
extractHeaderFieldsLiteral(const QByteArray &fetchRespRaw) {
    const QByteArray marker("BODY[HEADER.FIELDS");

    const auto markerPos  = fetchRespRaw.indexOf(marker);
    const auto braceOpen  = fetchRespRaw.indexOf('{', qMax<qsizetype>(markerPos, 0));
    const auto braceClose = fetchRespRaw.indexOf('}', braceOpen + 1);

    if (markerPos < 0 || braceOpen < 0 || braceClose < 0)
        return {};

    auto ok = false;
    const auto literalLen = fetchRespRaw.mid(braceOpen + 1, braceClose - braceOpen - 1).toInt(&ok);
    if (!ok || literalLen <= 0)
        return {};

    auto literalStart = braceClose + 1;
    if (literalStart + 1 < fetchRespRaw.size() && fetchRespRaw.at(literalStart) == '\r' && fetchRespRaw.at(literalStart + 1) == '\n') {
        literalStart += 2;
    }
    else if (literalStart < fetchRespRaw.size() && fetchRespRaw.at(literalStart) == '\n') {
        literalStart += 1;
    }

    if (literalStart < 0 || literalStart >= fetchRespRaw.size())
        return {};

    const auto literalEnd = qMin(fetchRespRaw.size(), literalStart + literalLen);
    if (literalEnd <= literalStart)
        return {};

    return QString::fromUtf8(fetchRespRaw.mid(literalStart, literalEnd - literalStart));
}

QString
extractGmailLabelsRaw(const QString &fetchResp) {
    static const QRegularExpression labelsRe("X-GM-LABELS\\s+\\(([^\\)]*)\\)"_L1,
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = labelsRe.globalMatch(fetchResp);

    QString first;
    QString firstNonEmpty;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m.hasMatch())
            continue;

        const QString captured = m.captured(1).trimmed();
        if (first.isEmpty())
            first = captured;
        if (!captured.isEmpty())
            firstNonEmpty = captured;
    }

    // Prefer a non-empty label list when multiple FETCH payloads are concatenated
    // (e.g. fallback label-only fetch appended after a full fetch).
    const auto result = firstNonEmpty.isEmpty() ? first : firstNonEmpty;
    return result;
}

QString
extractGmailCategoryFolder(const QString &fetchResp) {
    auto labels = extractGmailLabelsRaw(fetchResp).toLower();
    if (labels.isEmpty()) return {};

    labels.replace('"', ' ');
    labels.replace('\\', ' ');
    labels.replace('^', ' ');
    labels.replace(':', ' ');
    labels.replace('/', ' ');
    labels.replace('-', ' ');

    auto hasAny = [&](std::initializer_list<const char *> needles) {
        return std::ranges::any_of(needles, [&](const char* n) {
                return labels.contains(QString::fromLatin1(n));
            });
    };

    if (hasAny({"promotions", "promotion", "categorypromotions", "smartlabel_promo"})) return "[Gmail]/Categories/Promotions"_L1;
    if (hasAny({"social", "categorysocial", "smartlabel_social"})) return "[Gmail]/Categories/Social"_L1;
    if (hasAny({"purchases", "purchase", "categorypurchases", "smartlabel_receipt"})) return "[Gmail]/Categories/Purchases"_L1;
    if (hasAny({"updates", "update", "categoryupdates", "smartlabel_notification"})) return "[Gmail]/Categories/Updates"_L1;
    if (hasAny({"forums", "forum", "categoryforums", "smartlabel_group"})) return "[Gmail]/Categories/Forums"_L1;
    if (hasAny({"primary", "categorypersonal", "smartlabel_personal"})) return "[Gmail]/Categories/Primary"_L1;
    return {};
}

QString
extractInternalDateRaw(const QString &fetchResp) {
    static const QRegularExpression internalDateRe("INTERNALDATE \"([^\"]+)\""_L1);

    if (const auto im = internalDateRe.match(fetchResp); im.hasMatch()) {
        return im.captured(1);
    }
    return {};
}


QDateTime
parseBestDateTime(const QString &headerDate, const QString &fetchResp) {
    QDateTime dt;

    if (auto trimmedHeader = headerDate.trimmed(); !trimmedHeader.isEmpty()) {
        static const QRegularExpression commentRe("\\s*\\([^\\)]*\\)"_L1);
        trimmedHeader.remove(commentRe);

        dt = QDateTime::fromString(trimmedHeader, Qt::RFC2822Date);
        if (!dt.isValid()) {
            dt = QLocale::c().toDateTime(trimmedHeader, "ddd, d MMM yyyy hh:mm:ss t"_L1);
        }
        if (!dt.isValid()) {
            dt = QLocale::c().toDateTime(trimmedHeader, "d MMM yyyy hh:mm:ss t"_L1);
        }
    }

    if (!dt.isValid()) {
        if (const auto internal = extractInternalDateRaw(fetchResp); !internal.isEmpty()) {
            dt = QLocale::c().toDateTime(internal, "d-MMM-yyyy hh:mm:ss t"_L1);
        }
    }

    return dt;
}

} // namespace Imap::Parser
