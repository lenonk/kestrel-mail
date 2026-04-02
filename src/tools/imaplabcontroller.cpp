#include "imaplabcontroller.h"

#include "../core/accounts/accountrepository.h"
#include "../core/auth/filetokenvault.h"
#include "../core/utils.h"

#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QUrlQuery>
#include <QElapsedTimer>

using namespace Qt::Literals::StringLiterals;

namespace {
constexpr int kTlsTimeoutMs = 6000;
constexpr int kReadTimeoutMs = 3000;
constexpr int kTaggedReadTimeoutMs = 12000;

QString readUntilTagged(QSslSocket &socket, const QString &tag, int timeoutMs = kTaggedReadTimeoutMs)
{
    QByteArray all;
    const QByteArray tagBytes = tag.toUtf8();
    while (true) {
        if (!socket.waitForReadyRead(timeoutMs)) break;
        all += socket.readAll();
        if (all.contains(tagBytes + " OK") || all.contains(tagBytes + " NO") || all.contains(tagBytes + " BAD")) break;
    }
    return QString::fromUtf8(all);
}

QString xoauth2String(const QString &email, const QString &accessToken)
{
    // Keep control-A separators in distinct literals so C++ doesn't parse "\\x01a" as one hex escape.
    const QByteArray payload = "user=" + email.toUtf8() + "\x01" + "auth=Bearer " + accessToken.toUtf8() + "\x01" + "\x01";
    return QString::fromLatin1(payload.toBase64());
}
}

ImapLabController::ImapLabController(QObject *parent)
    : QObject(parent)
    , m_accountsRepo(new AccountRepository(this))
    , m_tokenVault(std::make_unique<FileTokenVault>())
{
    m_templates = {
        QVariantMap{{"name", "CAPABILITY"}, {"command", "CAPABILITY"}},
        QVariantMap{{"name", "SELECT INBOX"}, {"command", "SELECT \"INBOX\""}},
        QVariantMap{{"name", "UID SEARCH ALL"}, {"command", "UID SEARCH ALL"}},
        QVariantMap{{"name", "UID SEARCH incremental"}, {"command", "UID SEARCH UID 1:*"}},
        QVariantMap{{"name", "INBOX UID FETCH (Kestrel)"}, {"command", "UID FETCH <uid> (UID FLAGS INTERNALDATE X-GM-LABELS X-GM-MSGID BODY.PEEK[HEADER.FIELDS (FROM TO SENDER REPLY-TO RETURN-PATH SUBJECT DATE MESSAGE-ID LIST-ID BIMI-LOCATION LIST-PREVIEW X-PREHEADER X-MC-PREVIEW-TEXT X-ALT-DESCRIPTION)] BODY.PEEK[TEXT]<0.32000>)"}},
        QVariantMap{{"name", "UID FETCH labels only"}, {"command", "UID FETCH <uid> (X-GM-LABELS)"}},
        QVariantMap{{"name", "UID FETCH body window"}, {"command", "UID FETCH <uid> (BODY.PEEK[TEXT]<0.48000>)"}},
        QVariantMap{{"name", "UID FETCH BODYSTRUCTURE"}, {"command", "UID FETCH <uid> (BODYSTRUCTURE)"}},
        QVariantMap{{"name", "X-GM-RAW by msg-id"}, {"command", "UID SEARCH X-GM-RAW \"rfc822msgid:<message-id>\""}},
        QVariantMap{{"name", "X-GM-MSGID lookup"}, {"command", "UID SEARCH X-GM-MSGID <gm-msg-id>"}},
        QVariantMap{{"name", "LOGOUT"}, {"command", "LOGOUT"}},
    };
    refreshAccounts();
}

ImapLabController::~ImapLabController()
{
    closePersistentSession();
}

QVariantList ImapLabController::accounts() const { return m_accounts; }
QVariantList ImapLabController::commandTemplates() const { return m_templates; }
QString ImapLabController::selectedAccountEmail() const { return m_selectedAccountEmail; }
QString ImapLabController::commandText() const { return m_commandText; }
QString ImapLabController::output() const { return m_output; }
bool ImapLabController::appendOutput() const { return m_appendOutput; }
void ImapLabController::setAppendOutput(bool value)
{
    if (m_appendOutput == value) return;
    m_appendOutput = value;
    emit appendOutputChanged();
}
qint64 ImapLabController::elapsedMs() const { return m_elapsedMs; }
bool ImapLabController::running() const { return m_running; }

void ImapLabController::setSelectedAccountEmail(const QString &email)
{
    const QString normalized = Kestrel::normalizeEmail(email);
    if (m_selectedAccountEmail == normalized) return;
    closePersistentSession();
    m_selectedAccountEmail = normalized;
    emit selectedAccountEmailChanged();
}

void ImapLabController::setCommandText(const QString &text)
{
    if (m_commandText == text) return;
    m_commandText = text;
    emit commandTextChanged();
}

void ImapLabController::refreshAccounts()
{
    m_accounts = m_accountsRepo ? m_accountsRepo->accounts() : QVariantList{};
    if (m_selectedAccountEmail.isEmpty() && !m_accounts.isEmpty()) {
        setSelectedAccountEmail(m_accounts.first().toMap().value("email").toString());
    }
    emit accountsChanged();
}

void ImapLabController::applyTemplate(int index)
{
    if (index < 0 || index >= m_templates.size()) return;
    setCommandText(m_templates.at(index).toMap().value("command").toString());
}

QVariantMap ImapLabController::selectedAccount() const
{
    for (const QVariant &v : m_accounts) {
        const QVariantMap a = v.toMap();
        if (Kestrel::normalizeEmail(a.value("email").toString()) == m_selectedAccountEmail) {
            return a;
        }
    }
    return {};
}

QString ImapLabController::refreshAccessToken(const QVariantMap &account, const QString &email)
{
    if (!m_tokenVault) return {};
    const QString refreshToken = m_tokenVault->loadRefreshToken(email);
    if (refreshToken.isEmpty()) return {};

    const QString tokenUrl = account.value("oauthTokenUrl").toString();
    QString clientId = account.value("oauthClientId").toString().trimmed();
    QString clientSecret = account.value("oauthClientSecret").toString();

    if (tokenUrl.isEmpty() || clientId.isEmpty()) return {};

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(tokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded"_L1);

    QUrlQuery body;
    body.addQueryItem("grant_type", "refresh_token");
    body.addQueryItem("refresh_token", refreshToken);
    body.addQueryItem("client_id", clientId);
    if (!clientSecret.isEmpty()) body.addQueryItem("client_secret", clientSecret);

    QNetworkReply *reply = nam.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payload = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (!ok) return {};

    return QJsonDocument::fromJson(payload).object().value("access_token").toString();
}

bool ImapLabController::ensurePersistentSession(const QVariantMap &account, const QString &email, const QString &accessToken, QString *errorOut)
{
    const QString host = account.value("imapHost").toString();
    const int port = account.value("imapPort").toInt();
    if (host.isEmpty() || port <= 0) {
        if (errorOut) *errorOut = "Missing IMAP host/port"_L1;
        return false;
    }

    const bool sessionMatches = m_socket && m_socket->state() == QAbstractSocket::ConnectedState
            && m_sessionEmail == email && m_sessionHost == host && m_sessionPort == port;
    if (sessionMatches) return true;

    closePersistentSession();

    m_socket = new QSslSocket(this);
    m_socket->connectToHostEncrypted(host, static_cast<quint16>(port));
    if (!m_socket->waitForEncrypted(kTlsTimeoutMs)) {
        if (errorOut) *errorOut = "TLS connect failed: %1"_L1.arg(m_socket->errorString());
        closePersistentSession();
        return false;
    }
    if (!m_socket->waitForReadyRead(kReadTimeoutMs)) {
        if (errorOut) *errorOut = "No greeting"_L1;
        closePersistentSession();
        return false;
    }

    // consume greeting
    m_socket->readAll();

    const QString authTag = "x001"_L1;
    const QString authCmd = "%1 AUTHENTICATE XOAUTH2 %2\r\n"_L1.arg(authTag, xoauth2String(email, accessToken));
    m_socket->write(authCmd.toUtf8());
    m_socket->flush();
    const QString authResp = readUntilTagged(*m_socket, authTag);
    if (!authResp.contains(authTag + " OK"_L1, Qt::CaseInsensitive)) {
        if (errorOut) *errorOut = "AUTH failed: %1"_L1.arg(authResp.simplified());
        closePersistentSession();
        return false;
    }

    m_sessionEmail = email;
    m_sessionHost = host;
    m_sessionPort = port;
    return true;
}

QString ImapLabController::runImapCommand(const QString &command, qint64 *elapsedMsOut)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        if (elapsedMsOut) *elapsedMsOut = 0;
        return "No active IMAP session."_L1;
    }

    QElapsedTimer timer;
    timer.start();

    QString userCmd = command.trimmed();
    if (userCmd.isEmpty()) {
        if (elapsedMsOut) *elapsedMsOut = timer.elapsed();
        return "No command provided."_L1;
    }

    const QString cmdTag = "x002"_L1;
    if (!userCmd.startsWith(cmdTag + " ", Qt::CaseInsensitive)) {
        userCmd = cmdTag + " "_L1 + userCmd;
    }
    if (!userCmd.endsWith("\r\n")) userCmd += "\r\n";

    m_socket->write(userCmd.toUtf8());
    m_socket->flush();
    const QString resp = readUntilTagged(*m_socket, cmdTag);

    QString transcript;
    transcript += "C: " + userCmd;
    transcript += "S: " + resp + "\n";

    if (elapsedMsOut) *elapsedMsOut = timer.elapsed();
    return transcript;
}

void ImapLabController::closePersistentSession()
{
    if (!m_socket) return;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write("x999 LOGOUT\r\n");
        m_socket->flush();
        m_socket->disconnectFromHost();
    }
    m_socket->deleteLater();
    m_socket = nullptr;
    m_sessionEmail.clear();
    m_sessionHost.clear();
    m_sessionPort = 0;
}

void ImapLabController::runCurrentCommand()
{
    if (m_running) return;
    const QVariantMap account = selectedAccount();
    const QString email = Kestrel::normalizeEmail(account.value("email").toString());
    if (email.isEmpty()) {
        m_output = "No account selected";
        emit outputChanged();
        return;
    }

    m_running = true;
    emit runningChanged();

    const bool needsSessionConnect = (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState
                                      || m_sessionEmail != email
                                      || m_sessionHost != account.value("imapHost").toString()
                                      || m_sessionPort != account.value("imapPort").toInt());

    if (needsSessionConnect) {
        const QString token = refreshAccessToken(account, email);
        if (token.isEmpty()) {
            m_output = "Failed to refresh access token";
            m_running = false;
            emit outputChanged();
            emit runningChanged();
            return;
        }

        QString sessionError;
        if (!ensurePersistentSession(account, email, token, &sessionError)) {
            m_output = sessionError.isEmpty() ? "Failed to establish IMAP session"_L1 : sessionError;
            m_running = false;
            emit outputChanged();
            emit runningChanged();
            return;
        }
    }

    qint64 elapsed = 0;
    const QString runOutput = runImapCommand(m_commandText, &elapsed);
    if (m_appendOutput && !m_output.isEmpty()) {
        const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        m_output += QStringLiteral("\n\n----- %1 | %2 ms -----\n").arg(stamp).arg(elapsed);
        m_output += runOutput;
    } else {
        m_output = runOutput;
    }
    m_elapsedMs = elapsed;
    m_running = false;
    emit elapsedMsChanged();
    emit outputChanged();
    emit runningChanged();
}
