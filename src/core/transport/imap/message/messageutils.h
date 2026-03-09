#pragma once

#include <QString>
#include <QByteArray>

namespace Imap::MessageUtils {

// Address helpers
QString cleanAngle(const QString &s);
QString extractEmailAddress(const QString &input);
QString normalizeSenderValue(const QString &fromHeader, const QString &fallbackHeader);
QString sanitizeAddressHeader(QString v);

// Snippet helpers
QString decodeRfc2047(const QString &input);
qint32 snippetQualityScore(const QString &snippet);
bool snippetLooksLikeProtocolOrJunk(const QString &in);
bool snippetLooksGarbledBytes(const QString &s);
QString stripUnsafeUnicodeForSnippet(const QString &in);
QString cleanSnippet(const QString &raw);
QString extractReadableFallbackForSnippet(const QString &raw);
QString stripWhitespaceForSnippet(const QString &in);
QString stripMimeNoiseForSnippet(QString text);
QString compileDeterministicSnippet(const QString &subject,
                                   const QString &headerSource,
                                   const QByteArray &fetchRespRaw);

} // namespace Imap::MessageUtils
