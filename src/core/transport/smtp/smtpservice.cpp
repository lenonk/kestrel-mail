#include "smtpservice.h"

#include "../../accounts/accountrepository.h"
#include "../../auth/filetokenvault.h"

#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QUrl>
#include <QUrlQuery>
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
    return QStringLiteral("user=%1\u0001auth=Bearer %2\u0001\u0001").arg(email, token).toUtf8().toBase64();
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

} // namespace

SmtpService::SmtpService(AccountRepository *accounts, TokenVault *vault, QObject *parent)
    : QObject(parent), m_accounts(accounts), m_vault(vault)
{}

void SmtpService::sendEmail(const QVariantMap &params) {
    QtConcurrent::run([this, params]() {
        const auto result = doSend(params);
        QMetaObject::invokeMethod(this, [this, result]() {
            emit sendFinished(result.ok, result.message);
        }, Qt::QueuedConnection);
    });
}

SmtpService::SendResult SmtpService::doSend(const QVariantMap &params) {
    const QString fromEmail = params.value("fromEmail"_L1).toString().trimmed();
    const QStringList toList  = params.value("toList"_L1).toStringList();
    const QStringList ccList  = params.value("ccList"_L1).toStringList();
    const QStringList bccList = params.value("bccList"_L1).toStringList();
    const QString subject     = params.value("subject"_L1).toString();
    const QString body        = params.value("body"_L1).toString();

    if (fromEmail.isEmpty() || toList.isEmpty())
        return {false, "From and To addresses are required"_L1};

    // Resolve account config
    if (!m_accounts)
        return {false, "Account repository unavailable"_L1};

    QVariantMap account;
    for (const auto &a : m_accounts->accounts()) {
        const auto m = a.toMap();
        if (m.value("email"_L1).toString().compare(fromEmail, Qt::CaseInsensitive) == 0) {
            account = m;
            break;
        }
    }
    if (account.isEmpty())
        return {false, "Account not found: "_L1 + fromEmail};

    const QString smtpHost = account.value("smtpHost"_L1).toString();
    const int     smtpPort = account.value("smtpPort"_L1).toInt();
    if (smtpHost.isEmpty() || smtpPort <= 0)
        return {false, "SMTP host/port not configured for "_L1 + fromEmail};

    const QString accessToken = refreshAccessToken(fromEmail);
    if (accessToken.isEmpty())
        return {false, "Could not obtain access token for "_L1 + fromEmail};

    QSslSocket sock;
    sock.moveToThread(QThread::currentThread());

    const bool useSsl = (smtpPort == 465);

    if (useSsl) {
        sock.connectToHostEncrypted(smtpHost, static_cast<quint16>(smtpPort));
        if (!sock.waitForEncrypted(kSmtpConnectTimeoutMs))
            return {false, "TLS connect failed: "_L1 + sock.errorString()};
    } else {
        sock.connectToHost(smtpHost, static_cast<quint16>(smtpPort));
        if (!sock.waitForConnected(kSmtpConnectTimeoutMs))
            return {false, "TCP connect failed: "_L1 + sock.errorString()};
    }

    // Read greeting
    QByteArray resp = smtpReadResponse(sock);
    if (smtpCode(resp) != 220)
        return {false, "Unexpected greeting: "_L1 + QString::fromLatin1(resp.trimmed())};

    // EHLO
    smtpSend(sock, "EHLO kestrel\r\n");
    resp = smtpReadResponse(sock);
    if (smtpCode(resp) != 250)
        return {false, "EHLO failed: "_L1 + QString::fromLatin1(resp.trimmed())};

    // STARTTLS on port 587
    if (!useSsl) {
        smtpSend(sock, "STARTTLS\r\n");
        resp = smtpReadLine(sock);
        if (smtpCode(resp) != 220)
            return {false, "STARTTLS failed: "_L1 + QString::fromLatin1(resp.trimmed())};

        sock.startClientEncryption();
        if (!sock.waitForEncrypted(kSmtpConnectTimeoutMs))
            return {false, "TLS upgrade failed: "_L1 + sock.errorString()};

        // Re-EHLO after STARTTLS
        smtpSend(sock, "EHLO kestrel\r\n");
        resp = smtpReadResponse(sock);
        if (smtpCode(resp) != 250)
            return {false, "Post-STARTTLS EHLO failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // AUTH XOAUTH2
    const QByteArray authPayload = buildXOAuth2Payload(fromEmail, accessToken);
    smtpSend(sock, "AUTH XOAUTH2 " + authPayload + "\r\n");
    resp = smtpReadLine(sock);
    if (smtpCode(resp) != 235) {
        // Server may send a 334 base64-encoded error detail — send empty response to abort
        if (smtpCode(resp) == 334) {
            smtpSend(sock, "\r\n");
            resp = smtpReadLine(sock);
        }
        sock.disconnectFromHost();
        return {false, "SMTP authentication failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // MAIL FROM
    smtpSend(sock, "MAIL FROM:<" + fromEmail.toUtf8() + ">\r\n");
    resp = smtpReadLine(sock);
    if (smtpCode(resp) != 250) {
        sock.disconnectFromHost();
        return {false, "MAIL FROM rejected: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    // RCPT TO for all recipients
    const QStringList allRecipients = toList + ccList + bccList;
    for (const QString &recipient : allRecipients) {
        // Strip display name — extract just the addr-spec
        QString addr = recipient.trimmed();
        const int lt = addr.lastIndexOf('<');
        const int gt = addr.lastIndexOf('>');
        if (lt >= 0 && gt > lt)
            addr = addr.mid(lt + 1, gt - lt - 1).trimmed();

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

    // Build RFC 2822 message
    QByteArray msg;

    const QString fromDisplay = account.value("accountName"_L1).toString().trimmed();
    const QString fromFormatted = fromDisplay.isEmpty()
        ? "<"_L1 + fromEmail + ">"_L1
        : "\""_L1 + encodeDisplayName(fromDisplay) + "\" <"_L1 + fromEmail + ">"_L1;

    msg += "From: " + fromFormatted.toUtf8() + "\r\n";

    // To
    QStringList toFormatted;
    for (const auto &a : toList) toFormatted.append(formatAddress(a));
    msg += "To: " + toFormatted.join(", "_L1).toUtf8() + "\r\n";

    if (!ccList.isEmpty()) {
        QStringList ccFormatted;
        for (const auto &a : ccList) ccFormatted.append(formatAddress(a));
        msg += "Cc: " + ccFormatted.join(", "_L1).toUtf8() + "\r\n";
    }

    // Subject — encode if non-ASCII
    const QString subjectLine = subject.isEmpty() ? "(no subject)"_L1 : subject;
    bool subjectNeedsEncoding = false;
    for (const QChar c : subjectLine)
        if (c.unicode() > 127) { subjectNeedsEncoding = true; break; }
    if (subjectNeedsEncoding)
        msg += "Subject: =?UTF-8?B?" + subjectLine.toUtf8().toBase64() + "?=\r\n";
    else
        msg += "Subject: " + subjectLine.toUtf8() + "\r\n";

    msg += "MIME-Version: 1.0\r\n";
    msg += "Content-Type: text/plain; charset=UTF-8\r\n";
    msg += "Content-Transfer-Encoding: quoted-printable\r\n";
    msg += "\r\n";

    // Encode body as quoted-printable (simplified: just escape lines starting with '.')
    const QByteArray bodyUtf8 = body.toUtf8();
    for (const QByteArray &line : bodyUtf8.split('\n')) {
        QByteArray l = line;
        if (l.endsWith('\r'))
            l.chop(1);
        // Dot-stuffing
        if (l.startsWith('.'))
            msg += '.';
        msg += l + "\r\n";
    }

    msg += ".\r\n";

    sock.write(msg);
    sock.flush();

    resp = smtpReadLine(sock, kSmtpDataTimeoutMs);
    if (smtpCode(resp) != 250) {
        sock.disconnectFromHost();
        return {false, "Message submission failed: "_L1 + QString::fromLatin1(resp.trimmed())};
    }

    smtpSend(sock, "QUIT\r\n");
    sock.disconnectFromHost();

    return {true, "Message sent successfully"_L1};
}

QString SmtpService::refreshAccessToken(const QString &email) {
    if (!m_accounts || !m_vault)
        return {};

    QVariantMap account;
    for (const auto &a : m_accounts->accounts()) {
        const auto m = a.toMap();
        if (m.value("email"_L1).toString().compare(email, Qt::CaseInsensitive) == 0) {
            account = m;
            break;
        }
    }
    if (account.isEmpty())
        return {};

    const auto refreshToken = m_vault->loadRefreshToken(email);
    if (refreshToken.isEmpty())
        return {};

    const auto tokenUrl    = account.value("oauthTokenUrl"_L1).toString();
    auto clientId          = account.value("oauthClientId"_L1).toString().trimmed();
    auto clientSecret      = account.value("oauthClientSecret"_L1).toString();

    if (clientId.isEmpty() && account.value("providerId"_L1).toString() == "gmail"_L1) {
        clientId      = QString("");
        if (clientSecret.isEmpty())
            clientSecret = QString("");
    }

    if (tokenUrl.isEmpty() || clientId.isEmpty())
        return {};

    QNetworkAccessManager nam;
    QNetworkRequest req { QUrl(tokenUrl) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded"_L1);

    QUrlQuery body;
    body.addQueryItem("grant_type"_L1, "refresh_token"_L1);
    body.addQueryItem("refresh_token"_L1, refreshToken);
    body.addQueryItem("client_id"_L1, clientId);
    if (!clientSecret.isEmpty())
        body.addQueryItem("client_secret"_L1, clientSecret);

    auto *reply = nam.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto payload = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    if (!ok)
        return {};

    return QJsonDocument::fromJson(payload).object().value("access_token"_L1).toString();
}
