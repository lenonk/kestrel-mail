#include "smtpservice.h"

#include "../../accounts/accountrepository.h"
#include "../../auth/oauthservice.h"
#include "../../auth/tokenvault.h"
#include "../imap/message/bodyprocessor.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSslSocket>
#include <QUuid>
#include <QtConcurrent>

using namespace Qt::Literals::StringLiterals;

static constexpr int kSmtpConnectTimeoutMs  = 10000;
static constexpr int kSmtpReadTimeoutMs     = 8000;
static constexpr int kSmtpDataTimeoutMs     = 15000;

namespace {

// Read one line (CRLF-terminated) from socket, timeout in ms.
// Returns the full line including \r\n on success, empty on timeout/error.
QByteArray smtpReadLine(QSslSocket &sock, int timeoutMs = kSmtpReadTimeoutMs) {
    while (!sock.canReadLine()) {
        if (!sock.waitForReadyRead(timeoutMs))
            return {};
    }
    return sock.readLine();
}

// Read all continuation lines for a multi-line SMTP response (e.g. EHLO).
// Returns combined text. Returns empty string on timeout.
QByteArray smtpReadResponse(QSslSocket &sock, int timeoutMs = kSmtpReadTimeoutMs) {
    QByteArray all;
    while (true) {
        const QByteArray line = smtpReadLine(sock, timeoutMs);
        if (line.isEmpty())
            return {};
        all += line;
        // Continuation: "250-..." — keep reading. Final: "250 ..."
        if (line.size() >= 4 && line[3] == ' ')
            break;
        if (line.size() < 4)
            break;
    }
    return all;
}

// Returns the 3-digit status code from an SMTP response line, or 0 on parse error.
int smtpCode(const QByteArray &resp) {
    if (resp.size() < 3)
        return 0;
    bool ok = false;
    const int code = resp.left(3).toInt(&ok);
    return ok ? code : 0;
}

// Build XOAUTH2 bearer string: user=<email>\x01auth=Bearer <token>\x01\x01
QByteArray buildXOAuth2Payload(const QString &email, const QString &token) {
    return "user=%1\u0001auth=Bearer %2\u0001\u0001"_L1.arg(email, token).toUtf8().toBase64();
}

// Encode an RFC 2047 UTF-8 display name if it contains non-ASCII, else return as-is.
QString encodeDisplayName(const QString &name) {
    for (const QChar c : name) {
        if (c.unicode() > 127)
            return "=?UTF-8?B?"_L1 + QString::fromLatin1(name.toUtf8().toBase64()) + "?="_L1;
    }
    return name;
}

// Format a single "Display Name" <addr> or just <addr>
QString formatAddress(const QString &addrSpec) {
    const QString trimmed = addrSpec.trimmed();
    // Already formatted?
    if (trimmed.contains('<'))
        return trimmed;
    return '<' + trimmed + '>';
}

// Write a command and flush.
bool smtpSend(QSslSocket &sock, const QByteArray &cmd) {
    return sock.write(cmd) >= 0 && sock.flush();
}

QByteArray foldBase64(const QByteArray &raw)
{
    const QByteArray b64 = raw.toBase64();
    QByteArray out;
    out.reserve(b64.size() + b64.size() / 76 + 8);
    for (int i = 0; i < b64.size(); i += 76) {
        out += b64.mid(i, 76);
        out += "\r\n";
    }
    return out;
}

bool bodyLooksHtml(const QString &body)
{
    const QString s = body.trimmed();
    if (s.startsWith("<!DOCTYPE"_L1, Qt::CaseInsensitive)) return true;
    if (s.startsWith("<html"_L1, Qt::CaseInsensitive)) return true;
    if (s.contains(QRegularExpression("<\\s*(p|div|br|span|table|body|head|meta)\\b"_L1, QRegularExpression::CaseInsensitiveOption))) return true;
    return false;
}

QString extractAddrSpec(const QString &recipient)
{
    QString addr = recipient.trimmed();
    const int lt = addr.lastIndexOf('<');
    const int gt = addr.lastIndexOf('>');
    if (lt >= 0 && gt > lt)
        addr = addr.mid(lt + 1, gt - lt - 1).trimmed();
    return addr;
}

QString subjectHeader(const QString &subject)
{
    const QString subjectLine = subject.isEmpty() ? "(no subject)"_L1 : subject;
    for (const QChar c : subjectLine) {
        if (c.unicode() > 127) {
            return "Subject: =?UTF-8?B?"_L1 + QString::fromLatin1(subjectLine.toUtf8().toBase64()) + "?=\r\n"_L1;
        }
    }
    return "Subject: "_L1 + subjectLine + "\r\n"_L1;
}

QByteArray buildInlineBodyPart(const QString &body)
{
    const bool isHtml = bodyLooksHtml(body);
    const QString plain = isHtml ? Imap::BodyProcessor::flattenHtmlToText(body) : body;

    if (!isHtml) {
        QByteArray part;
        part += "Content-Type: text/plain; charset=UTF-8\r\n";
        part += "Content-Transfer-Encoding: base64\r\n\r\n";
        part += foldBase64(plain.toUtf8());
        return part;
    }

    const QByteArray altBoundary = "alt-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    QByteArray part;
    part += "Content-Type: multipart/alternative; boundary=\"" + altBoundary + "\"\r\n\r\n";

    part += "--" + altBoundary + "\r\n";
    part += "Content-Type: text/plain; charset=UTF-8\r\n";
    part += "Content-Transfer-Encoding: base64\r\n\r\n";
    part += foldBase64(plain.toUtf8());

    part += "--" + altBoundary + "\r\n";
    part += "Content-Type: text/html; charset=UTF-8\r\n";
    part += "Content-Transfer-Encoding: base64\r\n\r\n";
    part += foldBase64(body.toUtf8());

    part += "--" + altBoundary + "--\r\n";
    return part;
}

bool appendAttachmentPart(QByteArray &msg, const QString &path, QString *errorOut)
{
    QFileInfo fi(path);
    QFile file(path);
    if (!fi.exists() || !fi.isFile() || !file.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = "Attachment read failed: "_L1 + path;
        return false;
    }

    const QByteArray bytes = file.readAll();
    const QString filename = fi.fileName();
    const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();

    msg += "Content-Type: " + mime.toUtf8() + "; name=\"" + filename.toUtf8() + "\"\r\n";
    msg += "Content-Disposition: attachment; filename=\"" + filename.toUtf8() + "\"\r\n";
    msg += "Content-Transfer-Encoding: base64\r\n\r\n";
    msg += foldBase64(bytes);
    return true;
}

QByteArray dotStuff(const QByteArray &wire)
{
    QByteArray out;
    out.reserve(wire.size() + 64);
    const QList<QByteArray> lines = wire.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith('.')) out += '.';
        out += line + "\r\n";
    }
    return out;
}

} // namespace

SmtpService::SmtpService(AccountRepository *accounts, TokenVault *vault, QObject *parent)
    : QObject(parent), m_accounts(accounts), m_vault(vault)
{}

QVariantMap
SmtpService::findAccountByEmail(const QString &email) const {
    if (!m_accounts) {
        return {};
    }
    for (const auto &a : m_accounts->accounts()) {
        const auto m = a.toMap();
        if (m.value("email"_L1).toString().compare(email, Qt::CaseInsensitive) == 0) {
            return m;
        }
    }
    return {};
}

void
SmtpService::sendEmail(const QVariantMap &params) {
    (void)QtConcurrent::run([this, params]() {
        const auto result = doSend(params);
        QMetaObject::invokeMethod(this, [this, result]() {
            emit sendFinished(result.ok, result.message);
        }, Qt::QueuedConnection);
    });
}

SmtpService::SendResult
SmtpService::smtpConnect(QSslSocket &sock, const QString &smtpHost, const int smtpPort) const {
    sock.moveToThread(QThread::currentThread());

    const bool useSsl = (smtpPort == 465);

    if (useSsl) {
        sock.connectToHostEncrypted(smtpHost, static_cast<quint16>(smtpPort));
        if (!sock.waitForEncrypted(kSmtpConnectTimeoutMs)) {
            return {false, "TLS connect failed: "_L1 + sock.errorString()};
        }
    } else {
        sock.connectToHost(smtpHost, static_cast<quint16>(smtpPort));
        if (!sock.waitForConnected(kSmtpConnectTimeoutMs)) {
            return {false, "TCP connect failed: "_L1 + sock.errorString()};
        }
    }

    // Read greeting
    auto resp = smtpReadResponse(sock);
    if (smtpCode(resp) != 220) {
        return {false, "Unexpected greeting: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // EHLO
    smtpSend(sock, "EHLO kestrel\r\n");
    resp = smtpReadResponse(sock);
    if (smtpCode(resp) != 250) {
        return {false, "EHLO failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // STARTTLS on port 587
    if (!useSsl) {
        smtpSend(sock, "STARTTLS\r\n");
        resp = smtpReadLine(sock);
        if (smtpCode(resp) != 220) {
            return {false, "STARTTLS failed: "_L1 + QString::fromLatin1(resp.trimmed())};
        }

        sock.startClientEncryption();
        if (!sock.waitForEncrypted(kSmtpConnectTimeoutMs)) {
            return {false, "TLS upgrade failed: "_L1 + sock.errorString()};
        }

        // Re-EHLO after STARTTLS
        smtpSend(sock, "EHLO kestrel\r\n");
        resp = smtpReadResponse(sock);
        if (smtpCode(resp) != 250) {
            return {false, "Post-STARTTLS EHLO failed: "_L1 + QString::fromLatin1(resp.trimmed())};
        }
    }

    return {true, {}};
}

SmtpService::SendResult
SmtpService::smtpAuthenticate(QSslSocket &sock, const QString &fromEmail,
                              const QString &smtpHost, const int smtpPort,
                              const QString &accessToken) const {
    Q_UNUSED(smtpHost)
    Q_UNUSED(smtpPort)

    const auto authPayload = buildXOAuth2Payload(fromEmail, accessToken);
    smtpSend(sock, "AUTH XOAUTH2 " + authPayload + "\r\n");
    auto resp = smtpReadLine(sock);
    if (smtpCode(resp) != 235) {
        // Server may send a 334 base64-encoded error detail -- send empty response to abort
        if (smtpCode(resp) == 334) {
            smtpSend(sock, "\r\n");
            resp = smtpReadLine(sock);
        }
        sock.disconnectFromHost();
        return {false, "SMTP authentication failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    return {true, {}};
}

SmtpService::SendResult
SmtpService::smtpSendEnvelope(QSslSocket &sock, const QString &fromEmail,
                              const QStringList &allRecipients) const {
    // MAIL FROM
    smtpSend(sock, "MAIL FROM:<" + fromEmail.toUtf8() + ">\r\n");
    auto resp = smtpReadLine(sock);
    if (smtpCode(resp) != 250) {
        sock.disconnectFromHost();
        return {false, "MAIL FROM rejected: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // RCPT TO for all recipients
    for (const QString &recipient : allRecipients) {
        const auto addr = extractAddrSpec(recipient);
        smtpSend(sock, "RCPT TO:<" + addr.toUtf8() + ">\r\n");
        resp = smtpReadLine(sock);
        if (smtpCode(resp) != 250 && smtpCode(resp) != 251) {
            sock.disconnectFromHost();
            return {false, "RCPT TO rejected for "_L1 + addr + ": "_L1 + QString::fromLatin1(resp.trimmed())};
        }
    }

    // DATA
    smtpSend(sock, "DATA\r\n");
    resp = smtpReadLine(sock);
    if (smtpCode(resp) != 354) {
        sock.disconnectFromHost();
        return {false, "DATA command rejected: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    return {true, {}};
}

SmtpService::SendResult
SmtpService::doSend(const QVariantMap &params) {
    const auto fromEmail       = params.value("fromEmail"_L1).toString().trimmed();
    const auto toList          = params.value("toList"_L1).toStringList();
    const auto ccList          = params.value("ccList"_L1).toStringList();
    const auto bccList         = params.value("bccList"_L1).toStringList();
    const auto subject         = params.value("subject"_L1).toString();
    const auto body            = params.value("body"_L1).toString();
    const auto attachmentPaths = params.value("attachments"_L1).toStringList();

    if (fromEmail.isEmpty() || toList.isEmpty()) {
        return {false, "From and To addresses are required"_L1};
    }

    // Resolve account config
    const auto account = findAccountByEmail(fromEmail);
    if (account.isEmpty()) {
        return {false, "Account not found: "_L1 + fromEmail};
    }

    const auto smtpHost = account.value("smtpHost"_L1).toString();
    const auto smtpPort = account.value("smtpPort"_L1).toInt();
    if (smtpHost.isEmpty() || smtpPort <= 0) {
        return {false, "SMTP host/port not configured for "_L1 + fromEmail};
    }

    const auto accessToken = refreshAccessToken(fromEmail);
    if (accessToken.isEmpty()) {
        return {false, "Could not obtain access token for "_L1 + fromEmail};
    }

    // Connect, EHLO, and optionally STARTTLS
    QSslSocket sock;
    if (const auto r = smtpConnect(sock, smtpHost, smtpPort); !r.ok) {
        return r;
    }

    // AUTH XOAUTH2
    if (const auto r = smtpAuthenticate(sock, fromEmail, smtpHost, smtpPort, accessToken); !r.ok) {
        return r;
    }

    // MAIL FROM + RCPT TO + DATA
    const auto allRecipients = toList + ccList + bccList;
    if (const auto r = smtpSendEnvelope(sock, fromEmail, allRecipients); !r.ok) {
        return r;
    }

    // Build RFC 2822 + MIME message
    QByteArray msg;

    const auto fromDisplay = account.value("displayName"_L1).toString().trimmed().isEmpty()
            ? account.value("accountName"_L1).toString().trimmed()
            : account.value("displayName"_L1).toString().trimmed();
    const auto fromFormatted = fromDisplay.isEmpty()
        ? "<"_L1 + fromEmail + ">"_L1
        : "\""_L1 + encodeDisplayName(fromDisplay) + "\" <"_L1 + fromEmail + ">"_L1;

    msg += "From: " + fromFormatted.toUtf8() + "\r\n";

    QStringList toFormatted;
    for (const auto &a : toList) {
        toFormatted.append(formatAddress(a));
    }
    msg += "To: " + toFormatted.join(", "_L1).toUtf8() + "\r\n";

    if (!ccList.isEmpty()) {
        QStringList ccFormatted;
        for (const auto &a : ccList) {
            ccFormatted.append(formatAddress(a));
        }
        msg += "Cc: " + ccFormatted.join(", "_L1).toUtf8() + "\r\n";
    }

    msg += subjectHeader(subject).toUtf8();
    msg += "Date: " + QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy HH:mm:ss +0000"_L1).toUtf8() + "\r\n";
    msg += "Message-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8() + "@kestrel.mail>\r\n";
    msg += "MIME-Version: 1.0\r\n";

    if (attachmentPaths.isEmpty()) {
        msg += buildInlineBodyPart(body);
    } else {
        const auto mixedBoundary = "mix-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
        msg += "Content-Type: multipart/mixed; boundary=\"" + mixedBoundary + "\"\r\n\r\n";

        msg += "--" + mixedBoundary + "\r\n";
        msg += buildInlineBodyPart(body);

        for (const QString &path : attachmentPaths) {
            msg += "--" + mixedBoundary + "\r\n";
            QString attachErr;
            if (!appendAttachmentPart(msg, path, &attachErr)) {
                sock.disconnectFromHost();
                return {false, attachErr};
            }
        }

        msg += "--" + mixedBoundary + "--\r\n";
    }

    const auto wireMsg = dotStuff(msg) + ".\r\n";
    sock.write(wireMsg);
    sock.flush();

    const auto resp = smtpReadLine(sock, kSmtpDataTimeoutMs);
    if (smtpCode(resp) != 250) {
        sock.disconnectFromHost();
        return {false, "Message submission failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    smtpSend(sock, "QUIT\r\n");
    sock.disconnectFromHost();

    return {true, "Message sent successfully"_L1};
}

QString
SmtpService::refreshAccessToken(const QString &email) {
    if (!m_vault) {
        return {};
    }

    const auto account = findAccountByEmail(email);
    if (account.isEmpty()) {
        return {};
    }

    const auto refreshToken = m_vault->loadRefreshToken(email);
    if (refreshToken.isEmpty()) {
        return {};
    }

    const auto tokenUrl      = account.value("oauthTokenUrl"_L1).toString();
    const auto clientId      = account.value("oauthClientId"_L1).toString().trimmed();
    const auto clientSecret  = account.value("oauthClientSecret"_L1).toString();

    return OAuthService::refreshAccessToken(tokenUrl, refreshToken,
                                            clientId, clientSecret).accessToken;
}
