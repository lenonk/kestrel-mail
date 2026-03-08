#pragma once

#include <QString>


namespace Imap::AddressUtils {

/**
 * Remove angle brackets from a display name if present.
 * E.g., "John Doe <john@example.com>" -> "john@example.com"
 */
QString cleanAngle(const QString &s);

/**
 * Extract email address from a formatted address string.
 * Handles both angle-bracket format and plain email format.
 */
QString extractEmailAddress(const QString &input);

/**
 * Normalize a sender value to standard format "Name <email>".
 * Uses From header with Reply-To/Sender fallback for email extraction.
 */
QString normalizeSenderValue(const QString &fromHeader, const QString &fallbackHeader);

/**
 * Clean RFC2047-encoded address headers.
 * Removes empty encoded words and normalizes whitespace.
 */
QString sanitizeAddressHeader(QString v);

} // namespace Imap::AddressUtils

