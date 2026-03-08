#include "imapcommands.h"

namespace ImapCommands {

QByteArray
buildXOAuth2Command(const QString &tag, const QString &email, const QString &accessToken) {
    // XOAUTH2 SASL format: user=EMAIL\x01auth=Bearer TOKEN\x01\x01
    const QByteArray authRaw = QStringLiteral("user=%1\u0001auth=Bearer %2\u0001\u0001")
                                   .arg(email, accessToken)
                                   .toUtf8();
    
    return tag.toUtf8() + " AUTHENTICATE XOAUTH2 " + authRaw.toBase64() + "\r\n";
}

QByteArray
buildUidSearchCommand(const QString &tag, const QString &criteria) {
    return tag.toUtf8() + " UID SEARCH " + criteria.toUtf8() + "\r\n";
}

QByteArray
buildUidFetchCommand(const QString &tag, const QString &uidSet, const QString &items) {
    return tag.toUtf8() + " UID FETCH " + uidSet.toUtf8() + " " + items.toUtf8() + "\r\n";
}

QByteArray
buildSelectCommand(const QString &tag, const QString &mailbox) {
    const QString quotedMailbox = QStringLiteral("\"%1\"").arg(mailbox);
    return tag.toUtf8() + " SELECT " + quotedMailbox.toUtf8() + "\r\n";
}

QByteArray
buildSimpleCommand(const QString &tag, const QString &command) {
    return tag.toUtf8() + " " + command.toUtf8() + "\r\n";
}

} // namespace ImapCommands
