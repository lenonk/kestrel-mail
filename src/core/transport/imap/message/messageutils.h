#pragma once

#include <QString>
#include <QByteArray>

namespace Imap::MessageUtils {

// Address helpers
QString cleanAngle(const QString &s);
QString extractEmailAddress(const QString &input);
QString normalizeSenderValue(const QString &fromHeader, const QString &fallbackHeader);
QString sanitizeAddressHeader(QString v);

// Header decoding
QString decodeRfc2047(const QString &input);

// Snippet generation
// Mirrors Mailspring's approach: extract body text, strip whitespace, truncate at 400 chars.
QString compileDeterministicSnippet(const QString &subject,
                                    const QString &headerSource,
                                    const QByteArray &fetchRespRaw);

} // namespace Imap::MessageUtils
