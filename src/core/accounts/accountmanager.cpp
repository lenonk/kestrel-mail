#include "accountmanager.h"

#include "accountrepository.h"
#include "gmailaccount.h"
#include "imapaccount.h"
#include "iaccount.h"
#include "../transport/imap/imapservice.h"
#include "../transport/imap/googleapiservice.h"

using namespace Qt::Literals::StringLiterals;

AccountManager::AccountManager(AccountRepository *repo, DataStore *store,
                                ImapService *imap, GoogleApiService *googleApi,
                                TokenVault *vault, QObject *parent)
    : QObject(parent), m_repo(repo), m_store(store), m_imap(imap), m_googleApi(googleApi), m_vault(vault)
{
    if (m_repo) {
        connect(m_repo, &AccountRepository::accountsChanged, this, &AccountManager::rebuildFromRepository);
        rebuildFromRepository();
    }
}

QList<IAccount*>
AccountManager::accounts() const { return m_accounts; }

QList<QObject*>
AccountManager::accountsAsObjects() const {
    QList<QObject*> out;
    out.reserve(m_accounts.size());
    for (auto *a : m_accounts) out.push_back(a);
    return out;
}

QObject*
AccountManager::accountByEmail(const QString &email) const {
    const auto norm = email.trimmed().toLower();
    for (auto *a : m_accounts) {
        if (a->email().toLower() == norm) return a;
    }
    return nullptr;
}

void
AccountManager::rebuildFromRepository() {
    if (!m_repo) return;

    const auto configs = m_repo->accounts();

    // Build set of emails in the new config.
    QSet<QString> configEmails;
    for (const auto &v : configs)
        configEmails.insert(v.toMap().value("email"_L1).toString().trimmed().toLower());

    // Remove accounts no longer in the config.
    for (auto it = m_accounts.begin(); it != m_accounts.end(); ) {
        if (!configEmails.contains((*it)->email().toLower())) {
            if (m_imap)
                m_imap->unregisterAccount((*it)->email());
            (*it)->shutdown();
            (*it)->deleteLater();
            it = m_accounts.erase(it);
        } else {
            ++it;
        }
    }

    // Build set of emails already instantiated.
    QSet<QString> existingEmails;
    for (auto *a : m_accounts)
        existingEmails.insert(a->email().toLower());

    // Create new accounts.
    for (const auto &v : configs) {
        const auto config = v.toMap();
        const auto email = config.value("email"_L1).toString().trimmed().toLower();
        if (email.isEmpty() || existingEmails.contains(email)) continue;

        if (auto *acct = createAccount(config)) {
            m_accounts.push_back(acct);
            acct->initialize();
        }
    }

    emit accountsChanged();
}

IAccount*
AccountManager::createAccount(const QVariantMap &config) {
    const auto providerId = config.value("providerId"_L1).toString().toLower();
    const auto host = config.value("imapHost"_L1).toString().toLower();

    const bool isGmail = providerId == "gmail"_L1
                      || host.contains("gmail.com"_L1)
                      || host.contains("google"_L1);

    // Per-account ImapService — owns its own ConnectionPool.
    auto *accountImap = new ImapService(config, m_store, m_vault);

    IAccount *account = isGmail
        ? static_cast<IAccount*>(new GmailAccount(config, m_store, accountImap, m_googleApi, m_vault, this))
        : static_cast<IAccount*>(new ImapAccount(config, m_store, accountImap, m_vault, this));

    accountImap->setParent(account);

    // Register with the global ImapService so QML operations
    // (hydrate, move, mark-read, etc.) route to the right pool.
    if (m_imap) {
        const auto email = config.value("email"_L1).toString();
        m_imap->registerAccount(email, config, accountImap->pool());
    }

    return account;
}
