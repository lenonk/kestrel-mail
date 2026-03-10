#include "accountsetupcontroller.h"

#include "accountrepository.h"
#include "providerprofileservice.h"
#include "../auth/oauthservice.h"

#include <QDebug>

AccountSetupController::AccountSetupController(ProviderProfileService *profiles, OAuthService *oauth, AccountRepository *accounts, QObject *parent)
    : QObject(parent)
    , m_profiles(profiles)
    , m_oauth(oauth)
    , m_accounts(accounts)
{
    if (m_oauth) {
        connect(m_oauth, &OAuthService::authorizationCompleted, this, [this](bool ok, const QString &message) {
            Q_UNUSED(ok)
            m_statusMessage = message;
            const bool ready = m_oauth->hasStoredRefreshToken(m_email);
            if (m_oauthReady != ready) {
                m_oauthReady = ready;
                emit oauthReadyChanged();
            }
            emit statusMessageChanged();
        });
    }
}

QString AccountSetupController::email() const { return m_email; }

void AccountSetupController::setEmail(const QString &value)
{
    if (m_email == value) return;
    m_email = value;
    if (m_oauth) {
        const bool ready = m_oauth->hasStoredRefreshToken(m_email);
        if (m_oauthReady != ready) {
            m_oauthReady = ready;
            emit oauthReadyChanged();
        }
    }
    emit emailChanged();
}

QVariantMap AccountSetupController::selectedProvider() const { return m_selectedProvider; }
QString AccountSetupController::oauthUrl() const { return m_oauthUrl; }
QString AccountSetupController::statusMessage() const { return m_statusMessage; }
bool AccountSetupController::oauthReady() const { return m_oauthReady; }

void AccountSetupController::discoverProvider()
{
    if (!m_profiles) return;
    m_selectedProvider = m_profiles->discoverForEmail(m_email);
    const bool ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false;
    if (m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }
    m_statusMessage = QStringLiteral("Detected provider: %1").arg(m_selectedProvider.value("displayName").toString());
    emit selectedProviderChanged();
    emit statusMessageChanged();
}

void AccountSetupController::beginOAuth()
{
    if (!m_oauth) {
        return;
    }

    m_oauthUrl = m_oauth->startAuthorization(m_selectedProvider, m_email);
    m_statusMessage = m_oauth->lastStatus();

    emit oauthUrlChanged();
    emit statusMessageChanged();
}

bool AccountSetupController::completeOAuth(const QString &callbackOrCode)
{
    if (!m_oauth) return false;
    const bool ok = m_oauth->completeAuthorization(callbackOrCode);
    m_statusMessage = m_oauth->lastStatus();
    const bool ready = m_oauth->hasStoredRefreshToken(m_email);
    if (m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }
    emit statusMessageChanged();
    return ok;
}

bool AccountSetupController::saveCurrentAccount(const QString &accountName, const QString &encryption)
{
    if (!m_accounts) return false;

    const bool requiresOAuth = m_selectedProvider.value("supportsOAuth2").toBool();
    if (requiresOAuth && (!m_oauth || !m_oauth->hasStoredRefreshToken(m_email))) {
        m_statusMessage = QStringLiteral("Finish sign-in first (OAuth token not found yet).");
        emit statusMessageChanged();
        return false;
    }

    QVariantMap account;
    account.insert("email", m_email.trimmed().toLower());
    account.insert("providerId", m_selectedProvider.value("id"));
    account.insert("providerName", m_selectedProvider.value("displayName"));
    account.insert("imapHost", m_selectedProvider.value("imapHost"));
    account.insert("imapPort", m_selectedProvider.value("imapPort"));
    account.insert("smtpHost", m_selectedProvider.value("smtpHost"));
    account.insert("smtpPort", m_selectedProvider.value("smtpPort"));
    account.insert("accountName", accountName.trimmed().isEmpty() ? m_email.trimmed() : accountName.trimmed());
    account.insert("encryption", encryption);
    account.insert("authType", m_selectedProvider.value("supportsOAuth2").toBool() ? "oauth2" : "password");
    account.insert("oauthTokenUrl", m_selectedProvider.value("oauthTokenUrl"));
    account.insert("oauthClientId", m_selectedProvider.value("oauthClientId"));
    account.insert("oauthClientSecret", m_selectedProvider.value("oauthClientSecret"));

    m_accounts->addOrUpdateAccount(account);
    m_statusMessage = QStringLiteral("Account saved to local repository.");
    emit statusMessageChanged();
    return true;
}

bool AccountSetupController::hasTokenForEmail(const QString &email) const
{
    if (!m_oauth) return false;
    return m_oauth->hasStoredRefreshToken(email);
}

bool AccountSetupController::removeAccount(const QString &email)
{
    if (!m_accounts) return false;
    const QString normalized = email.trimmed().toLower();
    const bool removed = m_accounts->removeAccount(normalized);
    if (removed && m_oauth) {
        m_oauth->removeStoredRefreshToken(normalized);
    }
    const bool ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false;
    if (m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }
    m_statusMessage = removed
            ? QStringLiteral("Account removed.")
            : QStringLiteral("Account not found.");
    emit statusMessageChanged();
    return removed;
}
