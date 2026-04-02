#include "oauthservice.h"

#include "tokenvault.h"
#include "../utils.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
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

using namespace Qt::Literals::StringLiterals;

namespace {
constexpr quint16 kOAuthCallbackPort = 53682;
}

OAuthService::OAuthService(TokenVault *vault, QObject *parent)
    : QObject(parent)
    , m_vault(vault)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString OAuthService::pendingAuthUrl() const { return m_pendingAuthUrl; }
QString OAuthService::lastStatus() const { return m_lastStatus; }

QString OAuthService::startAuthorization(const QVariantMap &provider, const QString &email)
{
    const QString providerId = provider.value("id").toString();
    if (!provider.value("supportsOAuth2").toBool()) {
        m_lastStatus = "This mail provider needs manual sign-in setup in this build."_L1;
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
        m_lastStatus = "Sign-in is not configured yet for this provider in this build."_L1;
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
    const bool listening = m_callbackServer->listen(QHostAddress("127.0.0.1"_L1), kOAuthCallbackPort);

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

                const QString html =
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
                    "<!doctype html><html><head><meta charset='utf-8'><title>Kestrel Mail</title></head>"
                    "<body><h3>Sign-in complete</h3><p>You can close this tab and return to Kestrel Mail.</p></body></html>"_L1;
                socket->write(html.toUtf8());
                socket->flush();
                socket->disconnectFromHost();

                if (!path.isEmpty()) {
                    const QString callbackUrl = QStringLiteral("http://127.0.0.1:%1").arg(kOAuthCallbackPort) + path;
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
        m_lastStatus = QStringLiteral("OAuth callback listener failed on 127.0.0.1:%1. Close other apps using that port.").arg(kOAuthCallbackPort);
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return {};
    }

    QUrl url(authEndpoint);
    QUrlQuery q;
    q.addQueryItem("client_id"_L1, m_pendingClientId);
    q.addQueryItem("response_type"_L1, "code"_L1);
    q.addQueryItem("redirect_uri"_L1, QStringLiteral("http://127.0.0.1:%1/callback").arg(kOAuthCallbackPort));
    q.addQueryItem("scope"_L1, scope);
    q.addQueryItem("state"_L1, m_pendingState);
    q.addQueryItem("access_type"_L1, "offline"_L1);
    q.addQueryItem("prompt"_L1, "consent"_L1);
    q.addQueryItem("include_granted_scopes"_L1, "true"_L1);
    q.addQueryItem("code_challenge"_L1, challenge);
    q.addQueryItem("code_challenge_method"_L1, "S256"_L1);
    if (!m_pendingEmail.isEmpty()) {
        q.addQueryItem("login_hint"_L1, m_pendingEmail);
    }
    url.setQuery(q);

    m_pendingAuthUrl = url.toString();
    m_lastStatus = "OAuth sign-in started. Complete login in browser; Kestrel will capture callback automatically."_L1;
    emit pendingAuthUrlChanged();
    emit lastStatusChanged();
    return m_pendingAuthUrl;
}

void OAuthService::completeAuthorization(const QString &callbackOrCode)
{
    QString code;
    QString state;
    const QString input = callbackOrCode.trimmed();

    if (input.startsWith("http://"_L1) || input.startsWith("https://"_L1)) {
        const QUrl url(input);
        const QUrlQuery q(url);
        code = q.queryItemValue("code"_L1);
        state = q.queryItemValue("state"_L1);
    } else {
        code = input;
    }

    if (code.isEmpty()) {
        m_lastStatus = "Missing authorization code."_L1;
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return;
    }

    if (!state.isEmpty() && state != m_pendingState) {
        m_lastStatus = "OAuth state mismatch."_L1;
        emit lastStatusChanged();
        emit authorizationCompleted(false, m_lastStatus);
        return;
    }

    QNetworkRequest req{QUrl(m_pendingTokenUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded"_L1);

    QUrlQuery body;
    body.addQueryItem("grant_type"_L1, "authorization_code"_L1);
    body.addQueryItem("code"_L1, code);
    body.addQueryItem("redirect_uri"_L1, QStringLiteral("http://127.0.0.1:%1/callback").arg(kOAuthCallbackPort));
    body.addQueryItem("client_id"_L1, m_pendingClientId);
    body.addQueryItem("code_verifier"_L1, m_pendingVerifier);
    if (!m_pendingClientSecret.isEmpty()) {
        body.addQueryItem("client_secret"_L1, m_pendingClientSecret);
    }

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const QByteArray payload = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);

        if (!ok) {
            m_lastStatus = "Token exchange failed: %1"_L1.arg(QString::fromUtf8(payload));
            emit lastStatusChanged();
            emit authorizationCompleted(false, m_lastStatus);
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QString refreshToken = obj.value("refresh_token"_L1).toString();
        if (refreshToken.isEmpty()) {
            m_lastStatus = "Token exchange succeeded but no refresh_token returned."_L1;
            emit lastStatusChanged();
            emit authorizationCompleted(false, m_lastStatus);
            return;
        }

        QVariantMap profile;
        const QVariantMap claims = parseJwtPayloadClaims(obj.value("id_token"_L1).toString());
        const QString claimName = claims.value("name"_L1).toString().trimmed();
        const QString claimEmail = Kestrel::normalizeEmail(claims.value("email"_L1).toString());
        if (!claimName.isEmpty()) {
            profile.insert("displayName"_L1, claimName);
        }
        if (!claimEmail.isEmpty()) {
            profile.insert("email"_L1, claimEmail);
        }
        if (!profile.isEmpty()) {
            const QString key = Kestrel::normalizeEmail(m_pendingEmail);
            if (!key.isEmpty()) {
                m_profileByEmail.insert(key, profile);
            }
        }

        const bool stored = m_vault ? m_vault->storeRefreshToken(m_pendingEmail, refreshToken) : false;
        if (!stored) {
            m_lastStatus = "Token exchange succeeded but token vault write failed."_L1;
            emit lastStatusChanged();
            emit authorizationCompleted(false, m_lastStatus);
            return;
        }

        m_lastStatus = "OAuth complete. Refresh token stored."_L1;
        emit lastStatusChanged();
        emit authorizationCompleted(true, m_lastStatus);
    });
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
    return !m_vault->loadRefreshToken(Kestrel::normalizeEmail(email)).isEmpty();
}

bool OAuthService::removeStoredRefreshToken(const QString &email)
{
    if (!m_vault) return false;
    return m_vault->removeRefreshToken(Kestrel::normalizeEmail(email));
}

QVariantMap OAuthService::profileForEmail(const QString &email) const
{
    const QString key = Kestrel::normalizeEmail(email);
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
