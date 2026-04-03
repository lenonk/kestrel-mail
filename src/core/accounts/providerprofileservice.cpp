#include "providerprofileservice.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <ranges>

using namespace Qt::Literals::StringLiterals;

ProviderProfileService::ProviderProfileService(QObject *parent)
    : QObject(parent) {
    loadProfiles();
}

QVariantList
ProviderProfileService::providers() const {
    return m_providers;
}

QVariantMap
ProviderProfileService::discoverForEmail(const QString &email) const {
    const auto domain = email.section('@', 1, 1).trimmed().toLower();

    auto matchesDomain = [&](const QVariant &entry) {
        const auto domains = entry.toMap().value("domains"_L1).toList();
        return std::ranges::any_of(domains, [&](const QVariant &d) {
            return d.toString().trimmed().toLower() == domain;
        });
    };

    if (const auto it = std::ranges::find_if(m_providers, matchesDomain); it != m_providers.end())
        return it->toMap();

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

void
ProviderProfileService::loadProfiles() {
    QFile f(":/data/providers.json"_L1);

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "ProviderProfileService::loadProfiles: failed to open providers.json";
        return;
    }

    const auto doc = QJsonDocument::fromJson(f.readAll());
    for (const auto arr = doc.array(); const auto &v : arr) {
        m_providers << v.toObject().toVariantMap();
    }
}
