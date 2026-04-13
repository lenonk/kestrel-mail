#include "providerprofileservice.h"

#include <QDnsLookup>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSslSocket>
#include <QtConcurrent>

using namespace Qt::Literals::StringLiterals;

ProviderProfileService::ProviderProfileService(QObject *parent)
    : QObject(parent) {
    loadProfiles();
}

QVariantList
ProviderProfileService::providers() const {
    return m_providers;
}

bool
ProviderProfileService::discovering() const { return m_discovering; }

// ── Synchronous domain-only lookup ───────────────────────────────────────────

QVariantMap
ProviderProfileService::discoverForEmail(const QString &email) const {
    const auto domain = email.section(u'@', 1, 1).trimmed().toLower();
    auto result = matchByDomain(domain);
    if (!result.isEmpty()) return result;
    return buildGenericProfile(domain);
}

// ── Async full discovery ─────────────────────────────────────────────────────

void
ProviderProfileService::discoverForEmailAsync(const QString &email) {
    if (m_discovering) return;
    m_discovering = true;
    emit discoveringChanged();

    const auto domain = email.section(u'@', 1, 1).trimmed().toLower();

    (void)QtConcurrent::run([this, domain]() {
        // 1. Try direct domain match against providers.json.
        QVariantMap result = matchByDomain(domain);

        // 2. MX lookup if no domain match.
        if (result.isEmpty()) {
            QDnsLookup dns;
            dns.setType(QDnsLookup::MX);
            dns.setName(domain);
            QEventLoop loop;
            QObject::connect(&dns, &QDnsLookup::finished, &loop, &QEventLoop::quit);
            dns.lookup();
            loop.exec();

            if (dns.error() == QDnsLookup::NoError) {
                for (const auto &record : dns.mailExchangeRecords()) {
                    const auto exchange = record.exchange().toLower();
                    if (auto match = matchByMx(exchange); !match.isEmpty()) {
                        result = match;
                        break;
                    }
                }
            }
        }

        // 3. Fallback: generic with guessed hosts.
        if (result.isEmpty())
            result = buildGenericProfile(domain);

        // 4. Probe IMAP server CAPABILITY.
        const auto imapHost = result.value("imapHost"_L1).toString();
        const auto imapPort = result.value("imapPort"_L1).toInt();
        if (!imapHost.isEmpty() && imapPort > 0) {
            const auto caps = probeImapCapabilities(imapHost, imapPort);
            result.insert("authMethods"_L1, caps.value("authMethods"_L1));
        }

        // 5. Determine flow type.
        const auto authMethods = result.value("authMethods"_L1).toList();
        const bool hasXOAuth2 = authMethods.contains("XOAUTH2"_L1);
        const bool providerHasOAuth = result.value("supportsOAuth2"_L1).toBool()
                                   && !result.value("oauthClientId"_L1).toString().trimmed().isEmpty();
        result.insert("flowType"_L1, (hasXOAuth2 && providerHasOAuth) ? "oauth"_L1 : "manual"_L1);

        QMetaObject::invokeMethod(this, [this, result]() {
            m_discovering = false;
            emit discoveringChanged();
            emit discoveryFinished(result);
        }, Qt::QueuedConnection);
    });
}

// ── Helpers ──────────────────────────────────────────────────────────────────

QVariantMap
ProviderProfileService::matchByDomain(const QString &domain) const {
    for (const auto &entry : m_providers) {
        const auto map = entry.toMap();
        const auto domains = map.value("domains"_L1).toList();
        for (const auto &d : domains) {
            if (d.toString().trimmed().toLower() == domain)
                return map;
        }
    }
    return {};
}

QVariantMap
ProviderProfileService::matchByMx(const QString &mxHost) const {
    for (const auto &entry : m_providers) {
        const auto map = entry.toMap();
        for (const auto &p : map.value("mxPatterns"_L1).toList()) {
            const auto pattern = p.toString().toLower();
            if (pattern.startsWith(u'*')) {
                const auto suffix = pattern.mid(1); // e.g., ".google.com"
                if (mxHost.endsWith(suffix))
                    return map;
            } else if (mxHost == pattern) {
                return map;
            }
        }
    }
    return {};
}

QVariantMap
ProviderProfileService::probeImapCapabilities(const QString &host, const int port) {
    QVariantMap result;
    QSslSocket sock;
    sock.connectToHostEncrypted(host, static_cast<quint16>(port));
    if (!sock.waitForEncrypted(5000))
        return result;

    // Read greeting.
    if (!sock.waitForReadyRead(3000))
        return result;
    const auto greeting = QString::fromUtf8(sock.readAll());

    // Send CAPABILITY.
    sock.write("a001 CAPABILITY\r\n");
    sock.flush();
    if (!sock.waitForReadyRead(3000)) {
        sock.disconnectFromHost();
        return result;
    }
    const auto capResp = QString::fromUtf8(sock.readAll());

    // Parse AUTH= methods from both greeting and CAPABILITY response.
    QVariantList methods;
    static const QRegularExpression authRe("AUTH=([A-Z0-9_-]+)"_L1,
                                            QRegularExpression::CaseInsensitiveOption);
    auto it = authRe.globalMatch(greeting + u' ' + capResp);
    QSet<QString> seen;
    while (it.hasNext()) {
        const auto m = it.next().captured(1).toUpper();
        if (!seen.contains(m)) {
            seen.insert(m);
            methods.append(m);
        }
    }
    result.insert("authMethods"_L1, methods);

    // Logout.
    sock.write("a002 LOGOUT\r\n");
    sock.flush();
    sock.waitForReadyRead(1000);
    sock.disconnectFromHost();

    return result;
}

QVariantMap
ProviderProfileService::buildGenericProfile(const QString &domain) {
    QVariantMap generic;
    generic.insert("id"_L1,             "generic"_L1);
    generic.insert("displayName"_L1,    "Generic IMAP/SMTP"_L1);
    generic.insert("imapHost"_L1,       "imap."_L1 + domain);
    generic.insert("imapPort"_L1,       993);
    generic.insert("smtpHost"_L1,       "smtp."_L1 + domain);
    generic.insert("smtpPort"_L1,       587);
    generic.insert("supportsOAuth2"_L1, false);
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
    for (const auto arr = doc.array(); const auto &v : arr)
        m_providers << v.toObject().toVariantMap();
}
