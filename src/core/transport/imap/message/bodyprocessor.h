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
 * Decode transfer-encoded content (base64, quoted-printable, etc.)
 * Auto-detects encoding from Content-Transfer-Encoding header.
 */
QByteArray decodeTransferEncoded(const QByteArray &raw);

/**
 * Decode quoted-printable encoded content.
 */
QByteArray decodeQuotedPrintable(const QByteArray &in);

/**
 * Extract snippet from FETCH response by finding best text/plain or text/html part.
 * Returns cleaned, decoded snippet text.
 */
QString extractBodySnippetFromFetch(const QByteArray &fetchRespRaw);

/**
 * Extract HTML body from FETCH response using mailio parser.
 * Falls back to plain text wrapped in HTML if no HTML part found.
 */
QString extractBodyHtmlFromFetch(const QByteArray &fetchRespRaw);

/**
 * Extract plain text for snippet generation using mailio parser.
 * Tries plain text first, then HTML via stripHtmlTags.
 * Returns empty string on failure (no fallback message), allowing
 * the caller to fall through to raw-literal extraction paths.
 */
QString extractBodyTextForSnippet(const QByteArray &fetchRespRaw);

/**
 * Decode snippet literal with charset detection and cleaning.
 * Handles HTML preheader extraction and text cleaning.
 */
QString decodeSnippetLiteral(const QByteArray &literalBytes, const BodyPart &meta);

/**
 * Extract hidden preheader text from HTML (display:none blocks).
 */
QString extractHiddenPreheader(const QString &html);

/**
 * Strip HTML tags and return plain text.
 * Preserves line breaks from block elements.
 */
QString stripHtmlTags(const QString &html);

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