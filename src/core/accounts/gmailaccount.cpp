#include "gmailaccount.h"

#include "../store/datastore.h"
#include "../transport/imap/imapservice.h"
#include "../transport/imap/googleapiservice.h"

using namespace Qt::Literals::StringLiterals;

GmailAccount::GmailAccount(const QVariantMap &config, DataStore *store,
                             ImapService *imap, GoogleApiService *googleApi,
                             TokenVault *vault, QObject *parent)
    : BaseAccount(config, store, imap, vault, parent)
    , m_googleApi(googleApi)
{
    if (m_imap) {
        connect(m_imap, &ImapService::accountThrottled, this, [this](const QString &acct, const QString &) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_throttled = true;
                emit throttledChanged();
            }
        });
        connect(m_imap, &ImapService::accountUnthrottled, this, [this](const QString &acct) {
            if (acct.compare(m_email, Qt::CaseInsensitive) == 0) {
                m_throttled = false;
                emit throttledChanged();
            }
        });
    }
}

// -- Overrides ----------------------------------------------------------------

QString
GmailAccount::avatarSource() const {
    const auto custom = m_config.value("avatarIcon"_L1).toString().trimmed();
    if (!custom.isEmpty()) return custom;
    return "qrc:/qml/images/gmail_account_icon.svg"_L1;
}

QStringList
GmailAccount::categoryTabs() const {
    if (!m_store) return { "Primary"_L1 };
    return m_store->inboxCategoryTabs();
}

void
GmailAccount::syncAll() {
    BaseAccount::syncAll();
    if (m_googleApi)
        m_googleApi->refreshGooglePeopleAvatars(m_email);
}
