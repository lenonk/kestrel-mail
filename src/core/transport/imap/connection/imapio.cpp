#include "imapio.h"

namespace Imap::IO {

QByteArray
readUntilTaggedRaw(QSslSocket &socket, const QString &tag, const qint32 timeoutMs) {
    QByteArray all;
    const QByteArray tagBytes = tag.toUtf8();

    while (true) {
        if (!socket.waitForReadyRead(timeoutMs)) {
            break;
        }
        all += socket.readAll();
        if (all.contains(tagBytes + " OK") || all.contains(tagBytes + " NO") || all.contains(tagBytes + " BAD")) {
            break;
        }
    }
    return all;
}

QString
readUntilTagged(QSslSocket &socket, const QString &tag, const qint32 timeoutMs) {
    return QString::fromUtf8(readUntilTaggedRaw(socket, tag, timeoutMs));
}

} // namespace ImapIO
