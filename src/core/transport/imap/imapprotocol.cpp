#include "imapprotocol.h"
#include <QRegularExpression>
#include <QLocale>
#include <algorithm>

namespace ImapProtocol {

QString
buildUidSet(const QList<qint32> &uids) {
    if (uids.isEmpty())
        return {};
    
    QList<qint32> sorted = uids;
    std::ranges::sort(sorted);
    
    QStringList ranges;
    int rangeStart = sorted.first();
    int rangeEnd = rangeStart;
    
    for (qint32 i = 1; i < sorted.size(); ++i) {
        if (sorted[i] == rangeEnd + 1) {
            rangeEnd = sorted[i];
        }
        else {
            if (rangeStart == rangeEnd) {
                ranges << QString::number(rangeStart);
            }
            else {
                ranges << QStringLiteral("%1:%2").arg(rangeStart).arg(rangeEnd);
            }

            rangeStart = sorted[i];
            rangeEnd = rangeStart;
        }
    }
    
    if (rangeStart == rangeEnd) {
        ranges << QString::number(rangeStart);
    }
    else {
        ranges << QStringLiteral("%1:%2").arg(rangeStart).arg(rangeEnd);
    }
    
    return ranges.join(',');
}

QString
buildUidRange(const qint32 start, const qint32 end) {
    if (start == end)
        return QString::number(start);

    return QStringLiteral("%1:%2").arg(start).arg(end);
}

QString
buildSelectCommand(const QString &tag, const QString &folder) {
    return QStringLiteral("%1 SELECT \"%2\"\r\n").arg(tag, folder);
}

QString
buildUidSearchCommand(const QString &tag, const QString &criteria) {
    return QStringLiteral("%1 UID SEARCH %2\r\n").arg(tag, criteria);
}

QString
buildUidFetchCommand(const QString &tag, const QString &uidSpec, const QStringList &items) {
    return QStringLiteral("%1 UID FETCH %2 (%3)\r\n").arg(tag, uidSpec, items.join(' '));
}

QString
buildXListCommand(const QString &tag) {
    return QStringLiteral("%1 XLIST \"\" \"*\"\r\n").arg(tag);
}

QString
buildListCommand(const QString &tag) {
    return QStringLiteral("%1 LIST \"\" \"*\"\r\n").arg(tag);
}

bool
isUntaggedResponse(const QString &line) {
    return line.startsWith(QLatin1String("* "));
}

bool
isTaggedOk(const QString &line, const QString &tag) {
    return line.startsWith(tag + QLatin1String(" OK"));
}

bool
isTaggedNo(const QString &line, const QString &tag) {
    return line.startsWith(tag + QLatin1String(" NO"));
}

bool
isTaggedBad(const QString &line, const QString &tag) {
    return line.startsWith(tag + QLatin1String(" BAD"));
}

QList<qint32>
extractUidsFromSearch(const QString &response) {
    QList<qint32> uids;
    static const QRegularExpression searchRe(QStringLiteral("^\\*\\s+SEARCH\\s+(.*)$"), QRegularExpression::MultilineOption);
    static const QRegularExpression spacesRe(QStringLiteral("\\s+"));

    QRegularExpressionMatchIterator it = searchRe.globalMatch(response);
    
    while (it.hasNext()) {
        const auto m = it.next();
        const auto uidLine = m.captured(1).trimmed();

        for (const auto &parts = uidLine.split(spacesRe, Qt::SkipEmptyParts); const QString &part : parts) {
            auto ok = false;
            if (const int uid = part.toInt(&ok); ok && uid > 0) {
                uids.append(uid);
            }
        }
    }
    
    return uids;
}

QString
extractFolderFromSelect(const QString &response) {
    Q_UNUSED(response);
    // Implementation depends on what we're extracting
    // Placeholder for now
    return {};
}

qint32
extractExistsCount(const QString &response) {
    static const QRegularExpression existsRe(QStringLiteral("^\\*\\s+(\\d+)\\s+EXISTS"), QRegularExpression::MultilineOption);

    if (const auto m = existsRe.match(response); m.hasMatch()) {
        return m.captured(1).toInt();
    }

    return 0;
}

QString
formatImapDate(const QDate &date) {
    if (!date.isValid())
        return {};

    return QLocale::c().toString(date, QStringLiteral("dd-MMM-yyyy"));
}

} // namespace ImapProtocol
