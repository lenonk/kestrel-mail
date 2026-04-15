#pragma once

#include <QStringList>
#include <QByteArray>
#include <QDateTime>

namespace Imap::Parser {

/**
 * Parse UID SEARCH ALL response and return list of UIDs.
 * Example: "* SEARCH 1 2 3" → ["1", "2", "3"]
 */
QStringList parseUidSearchAll(const QString &resp);

/**
 * Parse UID SEARCH response with multiple SEARCH lines and return all UIDs.
 * Handles multiple "* SEARCH ..." lines in response.
 */
QStringList parseSearchIds(const QString &searchResp);

/**
 * Extract a header field value from raw headers.
 * Example: extractField(headers, "Subject") → "Hello World"
 */
QString extractField(const QString &input, const QString &field);

/**
 * Extract BODY[HEADER.FIELDS ...] literal from FETCH response.
 * Returns the literal content between {length}\r\n...\r\n markers.
 */
QString extractHeaderFieldsLiteral(const QByteArray &fetchRespRaw);

/**
 * Extract raw X-GM-LABELS value from FETCH response.
 * Example: "X-GM-LABELS (\\Inbox \"Category 1\")" → "\\Inbox \"Category 1\""
 */
QString extractGmailLabelsRaw(const QString &fetchResp);

/**
 * Infer Gmail category folder from X-GM-LABELS.
 * Returns folder like "[Gmail]/Categories/Promotions" or empty string.
 */
QString extractGmailCategoryFolder(const QString &fetchResp);

/**
 * Extract INTERNALDATE from FETCH response.
 * Example: "INTERNALDATE \"17-Jan-2024 10:30:00 +0000\"" → "17-Jan-2024 10:30:00 +0000"
 */
QString extractInternalDateRaw(const QString &fetchResp);

/**
 * Parse best available datetime from header Date or INTERNALDATE fallback.
 */
QDateTime parseBestDateTime(const QString &headerDate, const QString &fetchResp);

} // namespace Imap::Parser
