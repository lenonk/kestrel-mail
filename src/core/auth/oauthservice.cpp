#include "oauthservice.h"

#include "tokenvault.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QStringList>

OAuthService::OAuthService(TokenVault *vault, QObject *parent)
    : QObject(parent)
    , m_vault(vault)
{
}

QString OAuthService::pendingAuthUrl() const { return m_pendingAuthUrl; }
QString OAuthService::lastStatus() const { return m_lastStatus; }

QString OAuthService::startAuthorization(const QVariantMap &provider, const QString &email)
{
    const QString providerId = provider.value("id").toString();
    if (!provider.value("supportsOAuth2").toBool()) {
        m_lastStatus = QStringLiteral("This mail provider needs manual sign-in setup in this build.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return {};
    }

    const QString authEndpoint = provider.value("oauthAuthUrl").toString();
    m_pendingTokenUrl = provider.value("oauthTokenUrl").toString();
    const QString scope = provider.value("oauthScopes").toString();
    m_pendingClientId = provider.value("oauthClientId").toString().trimmed();
    m_pendingClientSecret = provider.value("oauthClientSecret").toString();

    if (m_pendingClientId.trimmed().isEmpty()) {
        m_lastStatus = QStringLiteral("Sign-in is not configured yet for this provider in this build.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return {};
    }

    m_pendingState = randomBase64Url(24);
    m_pendingVerifier = randomBase64Url(48);
    const QString challenge = sha256Base64Url(m_pendingVerifier);
    m_pendingProviderId = providerId;
    m_pendingEmail = email.trimmed();

    if (m_callbackServer) {
        m_callbackServer->close();
        m_callbackServer->deleteLater();
        m_callbackServer = nullptr;
    }

    m_callbackServer = new QTcpServer(this);
    const quint16 callbackPort = 53682;
    const bool listening = m_callbackServer->listen(QHostAddress(QStringLiteral("127.0.0.1")), callbackPort);

    QObject::connect(m_callbackServer, &QTcpServer::newConnection, this, [this]() {
        while (m_callbackServer->hasPendingConnections()) {
            QTcpSocket *socket = m_callbackServer->nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                const QByteArray req = socket->readAll();
                const QList<QByteArray> lines = req.split('\n');
                QString path;
                if (!lines.isEmpty()) {
                    const QList<QByteArray> parts = lines[0].trimmed().split(' ');
                    if (parts.size() >= 2) {
                        path = QString::fromUtf8(parts[1]);
                    }
                }

                const QString html = QStringLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
                    "<!doctype html><html><head><meta charset='utf-8'><title>Kestrel Mail</title></head>"
                    "<body><h3>Sign-in complete</h3><p>You can close this tab and return to Kestrel Mail.</p></body></html>");
                socket->write(html.toUtf8());
                socket->flush();
                socket->disconnectFromHost();

                if (!path.isEmpty()) {
                    const QString callbackUrl = QStringLiteral("http://127.0.0.1:53682") + path;
                    QMetaObject::invokeMethod(this, [this, callbackUrl]() {
                        completeAuthorization(callbackUrl);
                    }, Qt::QueuedConnection);
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    if (!listening) {
        qWarning().noquote() << "[oauth] callback listener failed"
                             << "error=" << m_callbackServer->errorString();
        m_lastStatus = QStringLiteral("OAuth callback listener failed on 127.0.0.1:53682. Close other apps using that port.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return {};
    }

    QUrl url(authEndpoint);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client_id"), m_pendingClientId);
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    q.addQueryItem(QStringLiteral("redirect_uri"), QStringLiteral("http://127.0.0.1:53682/callback"));
    q.addQueryItem(QStringLiteral("scope"), scope);
    q.addQueryItem(QStringLiteral("state"), m_pendingState);
    q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    q.addQueryItem(QStringLiteral("include_granted_scopes"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("code_challenge"), challenge);
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    if (!m_pendingEmail.isEmpty()) {
        q.addQueryItem(QStringLiteral("login_hint"), m_pendingEmail);
    }
    url.setQuery(q);

    m_pendingAuthUrl = url.toString();
    m_lastStatus = QStringLiteral("OAuth sign-in started. Complete login in browser; Kestrel will capture callback automatically.");
    emit pendingAuthUrlChanged();
    emit lastStatusChanged();
    return m_pendingAuthUrl;
}

bool OAuthService::completeAuthorization(const QString &callbackOrCode)
{
    QString code;
    QString state;
    const QString input = callbackOrCode.trimmed();

    if (input.startsWith(QStringLiteral("http://")) || input.startsWith(QStringLiteral("https://"))) {
        const QUrl url(input);
        const QUrlQuery q(url);
        code = q.queryItemValue(QStringLiteral("code"));
        state = q.queryItemValue(QStringLiteral("state"));
    } else {
        code = input;
    }

    if (code.isEmpty()) {
        m_lastStatus = QStringLiteral("Missing authorization code.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return false;
    }

    if (!state.isEmpty() && state != m_pendingState) {
        m_lastStatus = QStringLiteral("OAuth state mismatch.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return false;
    }

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(m_pendingTokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), QStringLiteral("http://127.0.0.1:53682/callback"));
    body.addQueryItem(QStringLiteral("client_id"), m_pendingClientId);
    body.addQueryItem(QStringLiteral("code_verifier"), m_pendingVerifier);
    if (!m_pendingClientSecret.isEmpty()) {
        body.addQueryItem(QStringLiteral("client_secret"), m_pendingClientSecret);
    }

    QNetworkReply *reply = nam.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payload = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();

    if (!ok) {
        m_lastStatus = QStringLiteral("Token exchange failed: %1").arg(QString::fromUtf8(payload));
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return false;
    }

    const QJsonObject obj = QJsonDocument::fromJson(payload).object();
    const QString refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    if (refreshToken.isEmpty()) {
        m_lastStatus = QStringLiteral("Token exchange succeeded but no refresh_token returned.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return false;
    }

    QVariantMap profile;
    const QVariantMap claims = parseJwtPayloadClaims(obj.value(QStringLiteral("id_token")).toString());
    const QString claimName = claims.value(QStringLiteral("name")).toString().trimmed();
    const QString claimEmail = claims.value(QStringLiteral("email")).toString().trimmed().toLower();
    if (!claimName.isEmpty()) {
        profile.insert(QStringLiteral("displayName"), claimName);
    }
    if (!claimEmail.isEmpty()) {
        profile.insert(QStringLiteral("email"), claimEmail);
    }
    if (!profile.isEmpty()) {
        const QString key = m_pendingEmail.trimmed().toLower();
        if (!key.isEmpty()) {
            m_profileByEmail.insert(key, profile);
        }
    }

    const bool stored = m_vault ? m_vault->storeRefreshToken(m_pendingEmail, refreshToken) : false;
    if (!stored) {
        m_lastStatus = QStringLiteral("Token exchange succeeded but token vault write failed.");
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return false;
    }

    m_lastStatus = QStringLiteral("OAuth complete. Refresh token stored.");
    emit lastStatusChanged();
    emit authorizationCompleted(true, m_lastStatus);
    return true;
}

QString OAuthService::randomBase64Url(int bytes)
{
    QByteArray data;
    data.resize(bytes);
    for (int i = 0; i < bytes; ++i) {
        data[i] = static_cast<char>(QRandomGenerator::global()->bounded(0, 256));
    }
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString OAuthService::sha256Base64Url(const QString &value)
{
    const QByteArray digest = QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256);
    return digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool OAuthService::hasStoredRefreshToken(const QString &email) const
{
    if (!m_vault) return false;
    return !m_vault->loadRefreshToken(email.trimmed().toLower()).isEmpty();
}

bool OAuthService::removeStoredRefreshToken(const QString &email)
{
    if (!m_vault) return false;
    return m_vault->removeRefreshToken(email.trimmed().toLower());
}

QVariantMap OAuthService::profileForEmail(const QString &email) const
{
    const QString key = email.trimmed().toLower();
    if (key.isEmpty()) {
        return {};
    }
    return m_profileByEmail.value(key);
}

QVariantMap OAuthService::parseJwtPayloadClaims(const QString &jwt)
{
    const QString token = jwt.trimmed();
    if (token.isEmpty()) {
        return {};
    }

    const QStringList parts = token.split('.');
    if (parts.size() < 2) {
        return {};
    }

    QByteArray payload = parts.at(1).toUtf8();
    payload.replace('-', '+');
    payload.replace('_', '/');
    const int remainder = payload.size() % 4;
    if (remainder == 2) payload.append("==");
    else if (remainder == 3) payload.append("=");
    else if (remainder == 1) return {};

    const QByteArray decoded = QByteArray::fromBase64(payload);
    const QJsonObject obj = QJsonDocument::fromJson(decoded).object();
    return obj.toVariantMap();
}
