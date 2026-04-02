#pragma once

#include <QObject>
#include <QVariantMap>

class ProviderProfileService;
class OAuthService;
class AccountRepository;

class AccountSetupController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString email READ email WRITE setEmail NOTIFY emailChanged)
    Q_PROPERTY(QVariantMap selectedProvider READ selectedProvider NOTIFY selectedProviderChanged)
    Q_PROPERTY(QString oauthUrl READ oauthUrl NOTIFY oauthUrlChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool oauthReady READ oauthReady NOTIFY oauthReadyChanged)

public:
    explicit AccountSetupController(ProviderProfileService *profiles, OAuthService *oauth, AccountRepository *accounts, QObject *parent = nullptr);

    void setEmail(const QString &value);

    [[nodiscard]] bool oauthReady() const;
    [[nodiscard]] QString email() const;
    [[nodiscard]] QString oauthUrl() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QVariantMap selectedProvider() const;

    Q_INVOKABLE void discoverProvider();
    Q_INVOKABLE void beginOAuth();
    Q_INVOKABLE void completeOAuth(const QString &callbackOrCode);
    Q_INVOKABLE bool saveCurrentAccount(const QString &accountName = QString(), const QString &encryption = QStringLiteral("TLS"));
    Q_INVOKABLE bool hasTokenForEmail(const QString &email) const;
    Q_INVOKABLE bool removeAccount(const QString &email);

signals:
    void emailChanged();
    void selectedProviderChanged();
    void oauthUrlChanged();
    void statusMessageChanged();
    void oauthReadyChanged();

private:
    ProviderProfileService *m_profiles;
    OAuthService *m_oauth;
    AccountRepository *m_accounts;
    QString m_email;
    QVariantMap m_selectedProvider;
    QString m_oauthUrl;
    QString m_statusMessage;
    bool m_oauthReady = false;
};
