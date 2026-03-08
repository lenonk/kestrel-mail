#pragma once

#include <QByteArray>
#include <QString>

namespace Mime {

// Attempt HTML extraction from IMAP FETCH BODY[] response using mailio MIME parsing.
// Returns empty string if no reliable HTML body is found.
QString extractHtmlWithMailio(const QByteArray &fetchRespRaw);

// Attempt plain-text extraction from IMAP FETCH BODY[] response using mailio MIME parsing.
// Returns empty string if no reliable text body is found.
QString extractPlainTextWithMailio(const QByteArray &fetchRespRaw);

}
