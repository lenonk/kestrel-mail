#pragma once

#include <QObject>
#include <QVariantMap>

class ProviderProfileService;
class OAuthService;
class AccountRepository;
class TokenVault;

class AccountSetupController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString email READ email WRITE setEmail NOTIFY emailChanged)
    Q_PROPERTY(QVariantMap selectedProvider READ selectedProvider NOTIFY selectedProviderChanged)
    Q_PROPERTY(QString oauthUrl READ oauthUrl NOTIFY oauthUrlChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool oauthReady READ oauthReady NOTIFY oauthReadyChanged)

    // Manual flow properties
    Q_PROPERTY(QString flowType READ flowType NOTIFY flowTypeChanged)
    Q_PROPERTY(QString imapHost READ imapHost WRITE setImapHost NOTIFY imapHostChanged)
    Q_PROPERTY(int imapPort READ imapPort WRITE setImapPort NOTIFY imapPortChanged)
    Q_PROPERTY(QString smtpHost READ smtpHost WRITE setSmtpHost NOTIFY smtpHostChanged)
    Q_PROPERTY(int smtpPort READ smtpPort WRITE setSmtpPort NOTIFY smtpPortChanged)
    Q_PROPERTY(QString imapUsername READ imapUsername WRITE setImapUsername NOTIFY imapUsernameChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(bool testPassed READ testPassed NOTIFY testPassedChanged)
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged)
    Q_PROPERTY(QString testResult READ testResult NOTIFY testResultChanged)
    Q_PROPERTY(QString avatarIcon READ avatarIcon WRITE setAvatarIcon NOTIFY avatarIconChanged)
    Q_PROPERTY(QString userDisplayName READ userDisplayName WRITE setUserDisplayName NOTIFY userDisplayNameChanged)

public:
    explicit AccountSetupController(ProviderProfileService *profiles, OAuthService *oauth,
                                     AccountRepository *accounts, TokenVault *vault,
                                     QObject *parent = nullptr);

    void setEmail(const QString &value);

    [[nodiscard]] bool oauthReady() const;
    [[nodiscard]] QString email() const;
    [[nodiscard]] QString oauthUrl() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QVariantMap selectedProvider() const;

    // Manual flow accessors
    [[nodiscard]] QString flowType() const;
    [[nodiscard]] QString imapHost() const;
    [[nodiscard]] int imapPort() const;
    [[nodiscard]] QString smtpHost() const;
    [[nodiscard]] int smtpPort() const;
    [[nodiscard]] QString imapUsername() const;
    [[nodiscard]] QString password() const;
    [[nodiscard]] bool testPassed() const;
    [[nodiscard]] bool testing() const;
    [[nodiscard]] QString testResult() const;

    [[nodiscard]] QString avatarIcon() const;
    [[nodiscard]] QString userDisplayName() const;

    void setImapHost(const QString &v);
    void setImapPort(int v);
    void setSmtpHost(const QString &v);
    void setSmtpPort(int v);
    void setImapUsername(const QString &v);
    void setPassword(const QString &v);
    void setAvatarIcon(const QString &v);
    void setUserDisplayName(const QString &v);

    Q_INVOKABLE void discoverProvider();
    Q_INVOKABLE void applyDiscoveryResult(const QVariantMap &result);
    Q_INVOKABLE void beginOAuth();
    Q_INVOKABLE void completeOAuth(const QString &callbackOrCode);
    Q_INVOKABLE bool saveCurrentAccount(const QString &accountName = QString(), const QString &encryption = QStringLiteral("TLS"));
    Q_INVOKABLE bool hasTokenForEmail(const QString &email) const;
    Q_INVOKABLE bool removeAccount(const QString &email);
    Q_INVOKABLE void testConnection();

signals:
    void emailChanged();
    void selectedProviderChanged();
    void oauthUrlChanged();
    void statusMessageChanged();
    void oauthReadyChanged();
    void flowTypeChanged();
    void imapHostChanged();
    void imapPortChanged();
    void smtpHostChanged();
    void smtpPortChanged();
    void imapUsernameChanged();
    void passwordChanged();
    void testPassedChanged();
    void testingChanged();
    void testResultChanged();
    void avatarIconChanged();
    void userDisplayNameChanged();

private:
    ProviderProfileService *m_profiles;
    OAuthService *m_oauth;
    AccountRepository *m_accounts;
    TokenVault *m_vault;
    QString m_email;
    QVariantMap m_selectedProvider;
    QString m_oauthUrl;
    QString m_statusMessage;
    bool m_oauthReady = false;

    // Manual flow state
    QString m_flowType;
    QString m_imapHost;
    int m_imapPort = 993;
    QString m_smtpHost;
    int m_smtpPort = 587;
    QString m_imapUsername;
    QString m_password;
    QString m_avatarIcon;
    QString m_userDisplayName;
    bool m_testPassed = false;
    bool m_testing = false;
    QString m_testResult;
};
