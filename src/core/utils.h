#pragma once
#include <QRegularExpression>
#include <QString>

namespace Kestrel {
inline QString normalizeEmail(const QString &email) { return email.trimmed().toLower(); }

// Shared regex for detecting whether a string contains HTML-ish markup.
// Used by mailioparser, htmlprocessor, imapservice, and datastore.
inline const QRegularExpression &htmlishRe() {
    static const QRegularExpression re(
        QStringLiteral(R"(<html|<body|<div|<table|<p|<br|<span|<img|<a\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}
}
