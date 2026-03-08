#include "addressutils.h"
#include <QRegularExpression>


namespace Imap::AddressUtils {

QString
cleanAngle(const QString &s) {
    auto out = s.trimmed();

    if (out.startsWith('<') && out.endsWith('>') && out.size() > 2) {
        out = out.mid(1, out.size() - 2);
    }

    return out;
}

QString
extractEmailAddress(const QString &input) {
    const auto s = input.trimmed();
    if (s.isEmpty()) { return {}; };

    static const QRegularExpression angleRe(QStringLiteral("<\\s*([^<>@\\s]+@[^<>@\\s]+)\\s*>"));
    if (const auto m1 = angleRe.match(s); m1.hasMatch())
        return m1.captured(1).trimmed();

    static const QRegularExpression plainRe(QStringLiteral("\\b([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (const auto m2 = plainRe.match(s); m2.hasMatch())
        return m2.captured(1).trimmed();

    return {};
}

QString
normalizeSenderValue(const QString &fromHeader, const QString &fallbackHeader) {
    auto from = cleanAngle(fromHeader).trimmed();
    auto email = extractEmailAddress(from);

    if (email.isEmpty()) {
        email = extractEmailAddress(fallbackHeader);
    }

    if (email.isEmpty()) {
        return from;
    }

    auto name = from;
    static const QRegularExpression angleBracketRe(QStringLiteral("<[^>]*>"));
    name.remove(angleBracketRe);
    name = name.trimmed();

    if ((name.startsWith('"') && name.endsWith('"')) || (name.startsWith('\'') && name.endsWith('\''))) {
        name = name.mid(1, name.size() - 2).trimmed();
    }
    else {
        if (name.startsWith('"') || name.startsWith('\'')) {
            name = name.mid(1).trimmed();
        }

        if (name.endsWith('"') || name.endsWith('\'')) {
            name.chop(1);
            name = name.trimmed();
        }
    }

    if (name.isEmpty() || name.compare(email, Qt::CaseInsensitive) == 0) {
        return email;
    }

    return QStringLiteral("%1 <%2>").arg(name, email);
}

QString
sanitizeAddressHeader(QString v) {
    static const QRegularExpression emptyRfc2047Re(QStringLiteral("=\\?[^?]+\\?[bBqQ]\\?\\?="));
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));

    v.replace(emptyRfc2047Re, QString());
    v.replace(whitespaceRe, QStringLiteral(" "));

    return v.trimmed();
}

} // namespace Imap::AddressUtils

