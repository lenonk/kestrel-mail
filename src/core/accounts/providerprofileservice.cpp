#include "providerprofileservice.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ProviderProfileService::ProviderProfileService(QObject *parent)
    : QObject(parent)
{
    loadProfiles();
}

QVariantList ProviderProfileService::providers() const
{
    return m_providers;
}

QVariantMap ProviderProfileService::discoverForEmail(const QString &email) const
{
    const QString domain = email.section('@', 1, 1).trimmed().toLower();

    // Hard-coded safety net for the two first-class OAuth providers.
    if (domain == QStringLiteral("gmail.com") || domain == QStringLiteral("googlemail.com")) {
        QVariantMap gmail;
        gmail.insert("id", "gmail");
        gmail.insert("displayName", "Gmail");
        gmail.insert("imapHost", "imap.gmail.com");
        gmail.insert("imapPort", 993);
        gmail.insert("smtpHost", "smtp.gmail.com");
        gmail.insert("smtpPort", 587);
        gmail.insert("supportsOAuth2", true);
        gmail.insert("oauthAuthUrl", "https://accounts.google.com/o/oauth2/v2/auth");
        gmail.insert("oauthTokenUrl", "https://oauth2.googleapis.com/token");
        gmail.insert("oauthScopes", "https://mail.google.com/ https://www.googleapis.com/auth/contacts.readonly");
        gmail.insert("oauthClientId", "");
        gmail.insert("oauthClientSecret", "");
        return gmail;
    }

    if (domain == QStringLiteral("outlook.com") || domain == QStringLiteral("hotmail.com")
        || domain == QStringLiteral("live.com") || domain == QStringLiteral("office365.com")) {
        QVariantMap ms;
        ms.insert("id", "microsoft365");
        ms.insert("displayName", "Microsoft 365 / Outlook");
        ms.insert("imapHost", "outlook.office365.com");
        ms.insert("imapPort", 993);
        ms.insert("smtpHost", "smtp.office365.com");
        ms.insert("smtpPort", 587);
        ms.insert("supportsOAuth2", true);
        ms.insert("oauthAuthUrl", "https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
        ms.insert("oauthTokenUrl", "https://login.microsoftonline.com/common/oauth2/v2.0/token");
        ms.insert("oauthScopes", "offline_access IMAP.AccessAsUser.All SMTP.Send");
        return ms;
    }

    for (const QVariant &entry : m_providers) {
        const QVariantMap map = entry.toMap();

        QStringList domains;
        const QVariant rawDomains = map.value("domains");
        if (rawDomains.canConvert<QStringList>()) {
            domains = rawDomains.toStringList();
        } else {
            const QVariantList list = rawDomains.toList();
            for (const QVariant &item : list) {
                domains << item.toString();
            }
        }

        for (const QString &d : domains) {
            if (d.trimmed().toLower() == domain) {
                return map;
            }
        }
    }

    QVariantMap generic;
    generic.insert("id", "generic");
    generic.insert("displayName", "Generic IMAP/SMTP");
    generic.insert("imapHost", "imap." + domain);
    generic.insert("imapPort", 993);
    generic.insert("smtpHost", "smtp." + domain);
    generic.insert("smtpPort", 587);
    generic.insert("supportsOAuth2", false);
    return generic;
}

void ProviderProfileService::loadProfiles()
{
    QFile f(QStringLiteral(":/data/providers.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto arr = doc.array();
    for (const QJsonValue &v : arr) {
        m_providers << v.toObject().toVariantMap();
    }
}
