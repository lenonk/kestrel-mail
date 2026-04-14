#pragma once

#include <QObject>
#include <QList>

class IAccount;
class AccountRepository;
class DataStore;
class ImapService;
class TokenVault;

/**
 * Owns and manages all IAccount instances.
 *
 * Creates the appropriate account implementation (GmailAccount, ImapAccount)
 * based on account configuration. Exposes the account list to QML.
 */
class AccountManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<QObject*> accounts READ accountsAsObjects NOTIFY accountsChanged)

public:
    explicit AccountManager(AccountRepository *repo, DataStore *store,
                            ImapService *imap, TokenVault *vault,
                            QObject *parent = nullptr);

    [[nodiscard]] QList<IAccount*> accounts() const;
    [[nodiscard]] QList<QObject*> accountsAsObjects() const;
    Q_INVOKABLE QObject* accountByEmail(const QString &email) const;

    void rebuildFromRepository();

signals:
    void accountsChanged();

private:
    IAccount* createAccount(const QVariantMap &config);

    AccountRepository *m_repo;
    DataStore *m_store;
    ImapService *m_imap;
    TokenVault *m_vault;
    QList<IAccount*> m_accounts;
};
