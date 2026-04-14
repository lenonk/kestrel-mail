#include "accountsetupcontroller.h"

#include "accountrepository.h"
#include "providerprofileservice.h"
#include "../auth/oauthservice.h"
#include "../auth/tokenvault.h"
#include "../transport/imap/connection/imapconnection.h"
#include "../utils.h"

#include <QtConcurrent>

using namespace Qt::Literals::StringLiterals;

AccountSetupController::AccountSetupController(ProviderProfileService *profiles, OAuthService *oauth,
                                                AccountRepository *accounts, TokenVault *vault,
                                                QObject *parent)
    : QObject(parent), m_profiles(profiles), m_oauth(oauth), m_accounts(accounts), m_vault(vault) {
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

// ── Basic accessors ──────────────────────────────────────────────────────────

QString AccountSetupController::email() const { return m_email; }
QVariantMap AccountSetupController::selectedProvider() const { return m_selectedProvider; }
QString AccountSetupController::oauthUrl() const { return m_oauthUrl; }
QString AccountSetupController::statusMessage() const { return m_statusMessage; }
bool AccountSetupController::oauthReady() const { return m_oauthReady; }

QString AccountSetupController::flowType() const { return m_flowType; }
QString AccountSetupController::imapHost() const { return m_imapHost; }
int AccountSetupController::imapPort() const { return m_imapPort; }
QString AccountSetupController::smtpHost() const { return m_smtpHost; }
int AccountSetupController::smtpPort() const { return m_smtpPort; }
QString AccountSetupController::imapUsername() const { return m_imapUsername; }
QString AccountSetupController::password() const { return m_password; }
bool AccountSetupController::testPassed() const { return m_testPassed; }
bool AccountSetupController::testing() const { return m_testing; }
QString AccountSetupController::testResult() const { return m_testResult; }

void AccountSetupController::setImapHost(const QString &v) { if (m_imapHost != v) { m_imapHost = v; emit imapHostChanged(); } }
void AccountSetupController::setImapPort(const int v) { if (m_imapPort != v) { m_imapPort = v; emit imapPortChanged(); } }
void AccountSetupController::setSmtpHost(const QString &v) { if (m_smtpHost != v) { m_smtpHost = v; emit smtpHostChanged(); } }
void AccountSetupController::setSmtpPort(const int v) { if (m_smtpPort != v) { m_smtpPort = v; emit smtpPortChanged(); } }
void AccountSetupController::setImapUsername(const QString &v) { if (m_imapUsername != v) { m_imapUsername = v; emit imapUsernameChanged(); } }
void AccountSetupController::setPassword(const QString &v) { if (m_password != v) { m_password = v; emit passwordChanged(); } }
void AccountSetupController::setAvatarIcon(const QString &v) { if (m_avatarIcon != v) { m_avatarIcon = v; emit avatarIconChanged(); } }
void AccountSetupController::setUserDisplayName(const QString &v) { if (m_userDisplayName != v) { m_userDisplayName = v; emit userDisplayNameChanged(); } }
QString AccountSetupController::avatarIcon() const { return m_avatarIcon; }
QString AccountSetupController::userDisplayName() const { return m_userDisplayName; }

// ── Email setter ─────────────────────────────────────────────────────────────

void
AccountSetupController::setEmail(const QString &value) {
    if (m_email == value) return;
    m_email = value;
    if (m_oauth) {
        if (const auto ready = m_oauth->hasStoredRefreshToken(m_email); m_oauthReady != ready) {
            m_oauthReady = ready;
            emit oauthReadyChanged();
        }
    }
    // Default username to email.
    if (m_imapUsername.isEmpty()) {
        m_imapUsername = value;
        emit imapUsernameChanged();
    }
    emit emailChanged();
}

// ── Provider discovery ───────────────────────────────────────────────────────

void
AccountSetupController::discoverProvider() {
    if (!m_profiles) return;
    m_selectedProvider = m_profiles->discoverForEmail(m_email);
    if (const auto ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false; m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }
    m_statusMessage = "Detected provider: %1"_L1.arg(m_selectedProvider.value("displayName"_L1).toString());
    emit selectedProviderChanged();
    emit statusMessageChanged();
}

void
AccountSetupController::applyDiscoveryResult(const QVariantMap &result) {
    m_selectedProvider = result;
    m_flowType = result.value("flowType"_L1).toString();
    m_imapHost = result.value("imapHost"_L1).toString();
    m_imapPort = result.value("imapPort"_L1).toInt();
    m_smtpHost = result.value("smtpHost"_L1).toString();
    m_smtpPort = result.value("smtpPort"_L1).toInt();
    m_testPassed = false;
    m_statusMessage = "Detected provider: %1"_L1.arg(result.value("displayName"_L1).toString());

    if (m_imapUsername.isEmpty())
        m_imapUsername = m_email;

    emit selectedProviderChanged();
    emit flowTypeChanged();
    emit imapHostChanged();
    emit imapPortChanged();
    emit smtpHostChanged();
    emit smtpPortChanged();
    emit statusMessageChanged();
    emit testPassedChanged();
}

// ── OAuth ────────────────────────────────────────────────────────────────────

void
AccountSetupController::beginOAuth() {
    if (!m_oauth) return;
    m_oauthUrl = m_oauth->startAuthorization(m_selectedProvider, m_email);
    m_statusMessage = m_oauth->lastStatus();
    emit oauthUrlChanged();
    emit statusMessageChanged();
}

void
AccountSetupController::completeOAuth(const QString &callbackOrCode) {
    if (!m_oauth) return;
    m_oauth->completeAuthorization(callbackOrCode);
}

// ── Test connection ──────────────────────────────────────────────────────────

void
AccountSetupController::testConnection() {
    if (m_testing) return;
    m_testing = true;
    m_testResult.clear();
    emit testingChanged();
    emit testResultChanged();

    const auto host = m_imapHost;
    const auto port = m_imapPort;
    const auto user = m_imapUsername.isEmpty() ? m_email : m_imapUsername;
    const auto pw = m_password;

    (void)QtConcurrent::run([this, host, port, user, pw]() {
        Imap::Connection conn;
        auto result = conn.connectAndAuth(host, port, user, pw, Imap::AuthMethod::Login);
        conn.disconnect();

        QMetaObject::invokeMethod(this, [this, result]() {
            m_testing = false;
            m_testPassed = result.success;
            m_testResult = result.success ? "Connection test passed."_L1 : result.message;
            m_statusMessage = m_testResult;
            emit testingChanged();
            emit testPassedChanged();
            emit testResultChanged();
            emit statusMessageChanged();
        }, Qt::QueuedConnection);
    });
}

// ── Save account ─────────────────────────────────────────────────────────────

bool
AccountSetupController::saveCurrentAccount(const QString &accountName, const QString &encryption) {
    if (!m_accounts) return false;

    const bool isManual = m_flowType == "manual"_L1;

    if (!isManual) {
        const auto requiresOAuth = m_selectedProvider.value("supportsOAuth2"_L1).toBool();
        if (requiresOAuth && (!m_oauth || !m_oauth->hasStoredRefreshToken(m_email))) {
            m_statusMessage = "Finish sign-in first (OAuth token not found yet)."_L1;
            emit statusMessageChanged();
            return false;
        }
    }

    QVariantMap account;
    account.insert("email"_L1, Kestrel::normalizeEmail(m_email));
    account.insert("providerId"_L1, m_selectedProvider.value("id"_L1));
    account.insert("providerName"_L1, m_selectedProvider.value("displayName"_L1));
    account.insert("imapHost"_L1, isManual ? m_imapHost : m_selectedProvider.value("imapHost"_L1).toString());
    account.insert("imapPort"_L1, isManual ? m_imapPort : m_selectedProvider.value("imapPort"_L1).toInt());
    account.insert("smtpHost"_L1, isManual ? m_smtpHost : m_selectedProvider.value("smtpHost"_L1).toString());
    account.insert("smtpPort"_L1, isManual ? m_smtpPort : m_selectedProvider.value("smtpPort"_L1).toInt());
    account.insert("accountName"_L1, accountName.trimmed().isEmpty() ? m_email.trimmed() : accountName.trimmed());
    account.insert("encryption"_L1, encryption);
    if (!m_avatarIcon.isEmpty())
        account.insert("avatarIcon"_L1, m_avatarIcon);
    if (!m_userDisplayName.isEmpty())
        account.insert("displayName"_L1, m_userDisplayName);

    if (isManual) {
        account.insert("authType"_L1, "password"_L1);
        if (m_vault && !m_password.isEmpty())
            m_vault->storePassword(Kestrel::normalizeEmail(m_email), m_password);
    } else {
        account.insert("authType"_L1, "oauth2"_L1);
        account.insert("oauthTokenUrl"_L1, m_selectedProvider.value("oauthTokenUrl"_L1));
        account.insert("oauthClientId"_L1, m_selectedProvider.value("oauthClientId"_L1));
        account.insert("oauthClientSecret"_L1, m_selectedProvider.value("oauthClientSecret"_L1));
    }

    if (m_oauth) {
        const auto profile = m_oauth->profileForEmail(m_email);
        if (const auto ownerName = profile.value("displayName"_L1).toString().trimmed(); !ownerName.isEmpty())
            account.insert("displayName"_L1, ownerName);
    }

    m_accounts->addOrUpdateAccount(account);
    m_statusMessage = "Account saved."_L1;
    emit statusMessageChanged();
    return true;
}

bool
AccountSetupController::hasTokenForEmail(const QString &email) const {
    if (!m_oauth) return false;
    return m_oauth->hasStoredRefreshToken(email);
}

bool
AccountSetupController::removeAccount(const QString &email) {
    if (!m_accounts) return false;
    const auto normalized = Kestrel::normalizeEmail(email);
    const auto removed = m_accounts->removeAccount(normalized);
    if (removed) {
        if (m_oauth) m_oauth->removeStoredRefreshToken(normalized);
        if (m_vault) m_vault->removePassword(normalized);
    }
    if (const auto ready = m_oauth ? m_oauth->hasStoredRefreshToken(m_email) : false; m_oauthReady != ready) {
        m_oauthReady = ready;
        emit oauthReadyChanged();
    }
    m_statusMessage = removed ? "Account removed."_L1 : "Account not found."_L1;
    emit statusMessageChanged();
    return removed;
}
