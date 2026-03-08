#pragma once

#include <QString>
#include <QByteArray>


namespace Imap::SnippetUtils {

/**
 * Decode RFC2047 encoded-word headers (e.g., "=?utf-8?B?SGVsbG8=?=").
 * Handles both Base64 (B) and Quoted-Printable (Q) encodings.
 */
QString decodeRfc2047(const QString &input);

/**
 * Calculate quality score for a snippet.
 * Higher score = better snippet quality.
 * Considers length, word count, CSS/URL presence, etc.
 */
qint32 snippetQualityScore(const QString &snippet);

/**
 * Check if snippet looks like protocol/IMAP junk or boilerplate.
 * Returns true for IMAP responses, newsletter boilerplate, etc.
 */
bool snippetLooksLikeProtocolOrJunk(const QString &in);

/**
 * Check if snippet looks like garbled binary/encoding issues.
 */
bool snippetLooksGarbledBytes(const QString &s);

/**
 * Strip unsafe Unicode control characters from snippet.
 * Removes control/format/surrogate/private-use characters.
 */
QString stripUnsafeUnicodeForSnippet(const QString &in);

/**
 * Clean snippet by removing HTML, headers, boilerplate, etc.
 * Main snippet cleaning pipeline.
 */
QString cleanSnippet(const QString &raw);

/**
 * Extract readable text fallback from raw content.
 * Used when structured parsing fails.
 */
QString extractReadableFallbackForSnippet(const QString &raw);

/**
 * Mailspring-style whitespace normalization.
 * Converts various whitespace chars to spaces, collapses runs.
 */
QString stripWhitespaceForSnippet(const QString &in);

/**
 * Strip MIME multipart noise for snippet extraction.
 * Removes boundaries, part headers, etc.
 */
QString stripMimeNoiseForSnippet(QString text);

/**
 * Compile a deterministic snippet from available sources.
 * Tries multiple strategies in order: preheader hints, HTML render,
 * BODY literal, readable fallback, body snippet fallback.
 * Returns cleaned snippet capped at 140 chars.
 */
QString compileDeterministicSnippet(const QString &subject,
                                   const QString &headerSource,
                                   const QByteArray &fetchRespRaw);

} // namespace Imap::SnippetUtils

