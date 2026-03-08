#pragma once

#include <QDate>

namespace ImapProtocol {

// UID set generation
QString buildUidSet(const QList<qint32> &uids);
QString buildUidRange(qint32 start, qint32 end);

// Command builders
QString buildSelectCommand(const QString &tag, const QString &folder);
QString buildUidSearchCommand(const QString &tag, const QString &criteria = QStringLiteral("ALL"));
QString buildUidFetchCommand(const QString &tag, const QString &uidSpec, const QStringList &items);
QString buildXListCommand(const QString &tag);
QString buildListCommand(const QString &tag);

// Response parsing
bool isUntaggedResponse(const QString &line);
bool isTaggedOk(const QString &line, const QString &tag);
bool isTaggedNo(const QString &line, const QString &tag);
bool isTaggedBad(const QString &line, const QString &tag);

// Extract data from responses
QList<int> extractUidsFromSearch(const QString &response);
QString extractFolderFromSelect(const QString &response);
int extractExistsCount(const QString &response);

// Date formatting
QString formatImapDate(const QDate &date);

} // namespace ImapProtocol
