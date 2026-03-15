#pragma once

#include <QObject>
#include <QVariantMap>

class AccountRepository;
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

    SendResult doSend(const QVariantMap &params);
    QString refreshAccessToken(const QString &email);

    AccountRepository *m_accounts = nullptr;
    TokenVault        *m_vault    = nullptr;
};
