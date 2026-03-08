#pragma once

#include <QString>

namespace Imap::AvatarResolver {

/**
 * Extract BIMI logo URL from BIMI-Location header.
 * Example: "BIMI-Location: v=BIMI1; l=https://example.com/logo.svg"
 */
QString extractBimiLogoUrl(const QString &headerSource);

/**
 * Extract sender domain from From header for avatar lookups.
 * Handles angle-bracketed and plain email formats.
 * Returns empty string for personal domains (gmail, outlook, etc.).
 */
QString senderDomainFromHeader(const QString &fromHeader);

/**
 * Parse BIMI logo URL from DNS TXT record.
 * Example: "v=BIMI1; l=https://example.com/logo.svg; a=..."
 */
QString parseBimiLogoFromTxtRecord(const QString &txtRaw);

/**
 * Resolve avatar URL via Google People API.
 * Requires valid OAuth2 access token.
 * Returns empty string if API unavailable or contact not found.
 * 
 * @param senderEmail Email address to lookup
 * @param accessToken OAuth2 access token for People API
 * @return Avatar URL or empty string
 */
QString resolveGooglePeopleAvatarUrl(const QString &senderEmail, const QString &accessToken);

/**
 * Fetch and cache avatar blob from URL.
 * Returns base64-encoded data URI or empty string on failure.
 * Caches results for 1 hour.
 * 
 * @param url Avatar URL (HTTP/HTTPS)
 * @return Data URI (data:image/...) or empty string
 */
QString fetchAvatarBlob(const QString &url);

/**
 * Extract List-ID domain for mailing list avatar lookups.
 * Returns normalized base domain for favicon lookup.
 */
QString extractListIdDomain(const QString &headerSource);

/**
 * Resolve BIMI logo URL via DNS over HTTPS (Google DNS).
 * Queries default._bimi.{domain} TXT record.
 * Returns empty string if not found or on timeout/error.
 *
 * @param domain Sender domain
 * @return BIMI logo URL or empty string
 */
QString resolveBimiLogoUrlViaDoh(const QString &domain);

/**
 * Resolve avatar via Gravatar using MD5 hash of the sender email.
 * Uses d=404 so a miss returns no image rather than a placeholder.
 * Returns a data URI on success, empty string on miss/timeout.
 * Results are cached per email address.
 *
 * @param email Sender email address
 * @return Data URI string or empty string
 */
QString resolveGravatarUrl(const QString &email);

/**
 * Resolve a favicon for a business domain via the DuckDuckGo favicon service.
 * Skips personal/consumer domains (gmail.com, outlook.com, etc.).
 * Returns a data URI (data:image/...;base64,...) on success, or empty on miss/timeout.
 * Results are cached per domain.
 *
 * @param domain Registrable sender domain (e.g. "firstam.com")
 * @return Data URI string or empty string
 */
QString resolveFaviconLogoUrl(const QString &domain);

} // namespace Imap::AvatarResolver

