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
