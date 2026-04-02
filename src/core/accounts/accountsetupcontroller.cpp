#include "accountsetupcontroller.h"

#include "accountrepository.h"
#include "providerprofileservice.h"
#include "../auth/oauthservice.h"
#include "../utils.h"

#include <QDebug>

using namespace Qt::Literals::StringLiterals;

AccountSetupController::AccountSetupController(ProviderProfileService *profiles, OAuthService *oauth, AccountRepository *accounts, QObject *parent)
    : QObject(parent) , m_profiles(profiles) , m_oauth(oauth) , m_accounts(accounts) {
    if (m_oauth) {
        connect(m_oauth, &OAuthService::authorizationCompleted, this, [this](const bool ok, const QString &message) {
            Q_UNUSED(ok)
            m_statusMessage = message;
            if (const bool ready = m_oauth->hasStoredRefreshToken(m_email); m_oauthReady != ready) {
                m_oauthReady = ready;
                emit oauthReadyChanged();
            }
            emit statusMessageChanged();
        });
    }
}

QString AccountSetupController::email() const { return m_email; }

void AccountSetupController::setEmail(const QString &value) {
    if (m_email == value) { return; }

    m_email = value;
    if (m_oauth) {
        if (const auto ready = m_oauth->hasStoredRefreshToken(m_email); m_oauthReady != ready) {
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

void AccountSetupController::discoverProvider() {
    if (!m_profiles) { return; }

    m_selectedProvider = m_profiles->discoverForEmail(m_email);
    if (const auto ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false; m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }

    m_statusMessage = "Detected provider: %1"_L1.arg(m_selectedProvider.value("displayName").toString());

    emit selectedProviderChanged();
    emit statusMessageChanged();
}

void AccountSetupController::beginOAuth() {
    if (!m_oauth) { return; }

    m_oauthUrl = m_oauth->startAuthorization(m_selectedProvider, m_email);
    m_statusMessage = m_oauth->lastStatus();

    emit oauthUrlChanged();
    emit statusMessageChanged();
}

void AccountSetupController::completeOAuth(const QString &callbackOrCode) {
    if (!m_oauth) { return; }

    m_oauth->completeAuthorization(callbackOrCode);
}

bool AccountSetupController::saveCurrentAccount(const QString &accountName, const QString &encryption) {
    if (!m_accounts) { return false; }

    const auto requiresOAuth = m_selectedProvider.value("supportsOAuth2").toBool();
    if (requiresOAuth && (!m_oauth || !m_oauth->hasStoredRefreshToken(m_email))) {
        m_statusMessage = "Finish sign-in first (OAuth token not found yet)."_L1;
        emit statusMessageChanged();
        return false;
    }

    QVariantMap account;
    account.insert("email", Kestrel::normalizeEmail(m_email));
    account.insert("providerId", m_selectedProvider.value("id"));
    account.insert("providerName", m_selectedProvider.value("displayName"));
    account.insert("imapHost", m_selectedProvider.value("imapHost"));
    account.insert("imapPort", m_selectedProvider.value("imapPort"));
    account.insert("smtpHost", m_selectedProvider.value("smtpHost"));
    account.insert("smtpPort", m_selectedProvider.value("smtpPort"));
    account.insert("accountName", accountName.trimmed().isEmpty() ? m_email.trimmed() : accountName.trimmed());

    if (m_oauth) {
        const auto profile = m_oauth->profileForEmail(m_email);
        if (const auto ownerName = profile.value("displayName").toString().trimmed(); !ownerName.isEmpty()) {
            account.insert("displayName", ownerName);
        }
    }

    account.insert("encryption", encryption);
    account.insert("authType", m_selectedProvider.value("supportsOAuth2").toBool() ? "oauth2" : "password");
    account.insert("oauthTokenUrl", m_selectedProvider.value("oauthTokenUrl"));
    account.insert("oauthClientId", m_selectedProvider.value("oauthClientId"));
    account.insert("oauthClientSecret", m_selectedProvider.value("oauthClientSecret"));

    m_accounts->addOrUpdateAccount(account);
    m_statusMessage = "Account saved to local repository."_L1;
    emit statusMessageChanged();

    return true;
}

bool AccountSetupController::hasTokenForEmail(const QString &email) const {
    if (!m_oauth) { return false; }

    return m_oauth->hasStoredRefreshToken(email);
}

bool AccountSetupController::removeAccount(const QString &email) {
    if (!m_accounts) { return false; }

    const auto normalized = Kestrel::normalizeEmail(email);
    const auto removed = m_accounts->removeAccount(normalized);
    if (removed && m_oauth) {
        m_oauth->removeStoredRefreshToken(normalized);
    }

    if (const auto ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false; m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }

    m_statusMessage = removed ? "Account removed."_L1 : "Account not found."_L1;

    emit statusMessageChanged();

    return removed;
}
