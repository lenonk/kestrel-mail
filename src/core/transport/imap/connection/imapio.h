#pragma once

#include <QSslSocket>

namespace Imap::IO {

// Default IMAP timeouts (milliseconds)
constexpr int kTlsTimeoutMs = 6000;
constexpr int kReadTimeoutMs = 3000;
constexpr int kTaggedReadTimeoutMs = 7000;
constexpr int kFetchReadTimeoutMs = 12000;

/**
 * Read from socket until a tagged response (tag + OK/NO/BAD) is received.
 * Returns raw bytes for efficient processing of large responses.
 * 
 * @param socket Active SSL socket
 * @param tag IMAP command tag (e.g., "a001")
 * @param timeoutMs Read timeout per iteration (default: kTaggedReadTimeoutMs)
 * @return Accumulated response bytes including tagged completion line
 */
QByteArray readUntilTaggedRaw(QSslSocket &socket, const QString &tag, qint32 timeoutMs = kTaggedReadTimeoutMs);

/**
 * Read from socket until a tagged response, returned as UTF-8 QString.
 * Convenience wrapper around readUntilTaggedRaw for text parsing.
 * 
 * @param socket Active SSL socket
 * @param tag IMAP command tag (e.g., "a001")
 * @param timeoutMs Read timeout per iteration (default: kTaggedReadTimeoutMs)
 * @return Response as QString
 */
QString readUntilTagged(QSslSocket &socket, const QString &tag, qint32 timeoutMs = kTaggedReadTimeoutMs);

} // namespace ImapIO
