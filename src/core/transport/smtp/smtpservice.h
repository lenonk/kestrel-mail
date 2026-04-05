#pragma once

#include <QObject>
#include <QVariantMap>

class AccountRepository;
class QSslSocket;
class TokenVault;

class SmtpService : public QObject
{
    Q_OBJECT
public:
    explicit SmtpService(AccountRepository *accounts, TokenVault *vault, QObject *parent = nullptr);

    // Send an email. Runs asynchronously via QtConcurrent.
    // params keys: fromEmail, toList (QStringList), ccList (QStringList),
    //              bccList (QStringList), subject (QString), body (QString)
    Q_INVOKABLE void sendEmail(const QVariantMap &params);

signals:
    void sendFinished(bool ok, const QString &message);

private:
    struct SendResult { bool ok = false; QString message; };

    QVariantMap findAccountByEmail(const QString &email) const;
    SendResult doSend(const QVariantMap &params);
    SendResult smtpConnect(QSslSocket &sock, const QString &smtpHost, int smtpPort) const;
    SendResult smtpAuthenticate(QSslSocket &sock, const QString &fromEmail,
                                const QString &smtpHost, int smtpPort,
                                const QString &accessToken) const;
    SendResult smtpSendEnvelope(QSslSocket &sock, const QString &fromEmail,
                                const QStringList &allRecipients) const;
    QString refreshAccessToken(const QString &email);

    AccountRepository *m_accounts = nullptr;
    TokenVault        *m_vault    = nullptr;
};
