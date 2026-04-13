#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class ProviderProfileService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
public:
    explicit ProviderProfileService(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList providers() const;

    // Synchronous domain-only lookup (for manual provider selection).
    Q_INVOKABLE QVariantMap discoverForEmail(const QString &email) const;

    // Async full discovery: domain match → MX lookup → IMAP CAPABILITY probe.
    Q_INVOKABLE void discoverForEmailAsync(const QString &email);

    [[nodiscard]] bool discovering() const;

signals:
    void discoveringChanged();
    void discoveryFinished(const QVariantMap &result);

private:
    QVariantList m_providers;
    bool m_discovering = false;

    void loadProfiles();
    QVariantMap matchByDomain(const QString &domain) const;
    QVariantMap matchByMx(const QString &mxHost) const;
    static QVariantMap probeImapCapabilities(const QString &host, int port);
    static QVariantMap buildGenericProfile(const QString &domain);
};
