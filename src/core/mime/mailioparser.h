#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

namespace Mime {

// Attempt HTML extraction from IMAP FETCH BODY[] response using mailio MIME parsing.
// Returns empty string if no reliable HTML body is found.
QString extractHtmlWithMailio(const QByteArray &fetchRespRaw);

// Attempt plain-text extraction from IMAP FETCH BODY[] response using mailio MIME parsing.
// Returns empty string if no reliable text body is found.
QString extractPlainTextWithMailio(const QByteArray &fetchRespRaw);

// Extract attachment metadata from full RFC822 payload in FETCH BODY[] response.
// Rows: { index, name, mimeType, bytes, canPreview }
QVariantList extractAttachmentsWithMailio(const QByteArray &fetchRespRaw);

// Extract raw attachment bytes by index from full RFC822 payload in FETCH BODY[] response.
QByteArray extractAttachmentDataWithMailio(const QByteArray &fetchRespRaw, int index);

}
