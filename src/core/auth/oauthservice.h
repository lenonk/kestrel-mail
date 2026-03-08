#pragma once

#include <QObject>
#include <QVariantMap>

class QTcpServer;

class TokenVault;

class OAuthService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pendingAuthUrl READ pendingAuthUrl NOTIFY pendingAuthUrlChanged)
    Q_PROPERTY(QString lastStatus READ lastStatus NOTIFY lastStatusChanged)
public:
    explicit OAuthService(TokenVault *vault, QObject *parent = nullptr);

    QString pendingAuthUrl() const;
    QString lastStatus() const;

    Q_INVOKABLE QString startAuthorization(const QVariantMap &provider, const QString &email);
    Q_INVOKABLE bool completeAuthorization(const QString &callbackOrCode);
    Q_INVOKABLE bool hasStoredRefreshToken(const QString &email) const;
    Q_INVOKABLE bool removeStoredRefreshToken(const QString &email);

signals:
    void pendingAuthUrlChanged();
    void lastStatusChanged();
    void authorizationCompleted(bool ok, const QString &message);

private:
    TokenVault *m_vault;
    QString m_pendingAuthUrl;
    QString m_lastStatus;
    QString m_pendingState;
    QString m_pendingVerifier;
    QString m_pendingProviderId;
    QString m_pendingEmail;
    QString m_pendingTokenUrl;
    QString m_pendingClientId;
    QString m_pendingClientSecret;
    QTcpServer *m_callbackServer = nullptr;

    static QString randomBase64Url(int bytes);
    static QString sha256Base64Url(const QString &value);
};
