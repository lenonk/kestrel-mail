#pragma once

#include <QObject>
#include <QVariantList>
#include <memory>

class AccountRepository;
class FileTokenVault;
class QSslSocket;

class ImapLabController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QVariantList commandTemplates READ commandTemplates CONSTANT)
    Q_PROPERTY(QString selectedAccountEmail READ selectedAccountEmail WRITE setSelectedAccountEmail NOTIFY selectedAccountEmailChanged)
    Q_PROPERTY(QString commandText READ commandText WRITE setCommandText NOTIFY commandTextChanged)
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)
    Q_PROPERTY(bool appendOutput READ appendOutput WRITE setAppendOutput NOTIFY appendOutputChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsedMsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit ImapLabController(QObject *parent = nullptr);
    ~ImapLabController() override;

    QVariantList accounts() const;
    QVariantList commandTemplates() const;

    QString selectedAccountEmail() const;
    void setSelectedAccountEmail(const QString &email);

    QString commandText() const;
    void setCommandText(const QString &text);

    QString output() const;
    bool appendOutput() const;
    void setAppendOutput(bool value);
    qint64 elapsedMs() const;
    bool running() const;

    Q_INVOKABLE void refreshAccounts();
    Q_INVOKABLE void applyTemplate(int index);
    Q_INVOKABLE void runCurrentCommand();

signals:
    void accountsChanged();
    void selectedAccountEmailChanged();
    void commandTextChanged();
    void outputChanged();
    void appendOutputChanged();
    void elapsedMsChanged();
    void runningChanged();

private:
    QVariantMap selectedAccount() const;
    QString refreshAccessToken(const QVariantMap &account, const QString &email);
    bool ensurePersistentSession(const QVariantMap &account, const QString &email, const QString &accessToken, QString *errorOut);
    QString runImapCommand(const QString &command, qint64 *elapsedMsOut);
    void closePersistentSession();

    AccountRepository *m_accountsRepo = nullptr;
    std::unique_ptr<FileTokenVault> m_tokenVault;
    QVariantList m_accounts;
    QVariantList m_templates;
    QString m_selectedAccountEmail;
    QString m_commandText;
    QString m_output;
    bool m_appendOutput = false;
    qint64 m_elapsedMs = 0;
    bool m_running = false;
    QSslSocket *m_socket = nullptr;
    QString m_sessionEmail;
    QString m_sessionHost;
    int m_sessionPort = 0;
};
