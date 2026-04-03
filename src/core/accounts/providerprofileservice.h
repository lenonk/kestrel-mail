#pragma once

#include <QObject>
#include <QVariantList>

class ProviderProfileService : public QObject {
    Q_OBJECT
public:
    explicit ProviderProfileService(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList providers() const;
    Q_INVOKABLE QVariantMap discoverForEmail(const QString &email) const;

private:
    QVariantList m_providers;
    void loadProfiles();
};
