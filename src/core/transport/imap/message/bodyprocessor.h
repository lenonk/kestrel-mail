#pragma once

#include <QList>
#include <utility>

namespace Imap::BodyProcessor {
/**
 * Parsed MIME body part from IMAP BODYSTRUCTURE.
 */
struct BodyPart {
    QString partId;        // e.g., "1", "1.2"
    QString type;          // e.g., "TEXT", "IMAGE"
    QString subtype;       // e.g., "PLAIN", "HTML"
    QString encoding;      // e.g., "quoted-printable", "base64"
    QString charset;       // e.g., "utf-8", "iso-8859-1"
    QString filename;      // attachment/display filename when present
    int bytes = 0;
    bool isAttachment = false;
    bool isInline = false;     // Content-Disposition: inline — render in body, not as card
    int score = 0;         // Higher score = better for snippet extraction
};

/**
 * Parse IMAP BODYSTRUCTURE response to extract text parts suitable for snippet extraction.
 * Returns parts sorted by score (best first).
 */
QList<BodyPart> parsePreferredTextParts(const QString &bodyStructureResponse);

/**
 * Get part IDs from BODYSTRUCTURE, sorted by preference.
 * Returns list of part IDs like ["1", "1.2", "2"].
 */
QStringList preferredSnippetPartIds(const QString &bodyStructureResponse);

/**
 * Get the single best part for snippet extraction from BODYSTRUCTURE.
 * Prefers text/plain, but uses text/html if plain is suspiciously short.
 */
BodyPart preferredSnippetPart(const QString &bodyStructureResponse);

/**
 * Parse IMAP BODYSTRUCTURE response and return attachment parts.
 * Returns parts that have a filename or whose type is not TEXT/MULTIPART
 * (images, application/pdf, audio, video, etc.).
 */
QList<BodyPart> parseAttachmentParts(const QString &bodyStructureResponse);

/**
 * Decode transfer-encoded content (base64, quoted-printable, etc.)
 * Auto-detects encoding from Content-Transfer-Encoding header.
 */
QByteArray decodeTransferEncoded(const QByteArray &raw);

/**
 * Decode quoted-printable encoded content.
 */
QByteArray decodeQuotedPrintable(const QByteArray &in);

/**
 * Extract HTML body from FETCH response using mailio parser.
 * Falls back to plain text wrapped in HTML if no HTML part found.
 */
QString extractBodyHtmlFromFetch(const QByteArray &fetchRespRaw);

/**
 * Extract plain text for snippet generation using mailio parser.
 * Prefers text/plain; falls back to HTML flattening via flattenHtmlToText.
 * Detects HTML-as-plain (HTML-only emails) and routes through the HTML path.
 * Returns empty on total failure, allowing the caller to fall back.
 */
QString extractBodyTextForSnippet(const QByteArray &fetchRespRaw);

/**
 * Extract hidden preheader text from HTML (display:none blocks used by newsletters).
 */
QString extractHiddenPreheader(const QString &html);

namespace Private_ {

    class BodyStructureParser {
    public:
        explicit BodyStructureParser(QString text);

        QList<BodyPart> parsePreferredTextParts();

    private:
        QString m_text;
        qsizetype m_i = 0;

        bool consume(QChar c);
        void parseBody(const QString &partPrefix, QList<BodyPart> &out);
        void parseMultipart(const QString &partPrefix, QList<BodyPart> &out);
        void parseSinglepart(const QString &partPrefix, QList<BodyPart> &out);
        void skipWs();
        void skipAny();
        void skipRemainingInList();

        QString parseAtomOrQuoted();
    };
} // namespace Private_
} // namespace Imap::BodyProcessor