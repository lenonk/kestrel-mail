#include "accountrepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

AccountRepository::AccountRepository(QObject *parent)
    : QObject(parent)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/kestrel-mail");
    QDir().mkpath(base);
    m_path = base + QStringLiteral("/accounts.json");
    load();
}

QVariantList AccountRepository::accounts() const
{
    return m_accounts;
}

void AccountRepository::addOrUpdateAccount(const QVariantMap &account)
{
    const QString email = account.value("email").toString().trimmed().toLower();
    if (email.isEmpty()) {
        return;
    }

    bool updated = false;
    for (int i = 0; i < m_accounts.size(); ++i) {
        QVariantMap existing = m_accounts[i].toMap();
        if (existing.value("email").toString().trimmed().toLower() == email) {
            for (auto it = account.constBegin(); it != account.constEnd(); ++it) {
                existing.insert(it.key(), it.value());
            }
            m_accounts[i] = existing;
            updated = true;
            break;
        }
    }

    if (!updated) {
        m_accounts.push_back(account);
    }

    save();
    emit accountsChanged();
}

bool AccountRepository::removeAccount(const QString &email)
{
    const QString normalized = email.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_accounts.size(); ++i) {
        const QVariantMap existing = m_accounts[i].toMap();
        if (existing.value("email").toString().trimmed().toLower() == normalized) {
            m_accounts.removeAt(i);
            save();
            emit accountsChanged();
            return true;
        }
    }
    return false;
}

void AccountRepository::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    m_accounts.clear();

    bool migrated = false;
    for (const auto &v : arr) {
        QVariantMap account = v.toObject().toVariantMap();
        const QString providerId = account.value("providerId").toString();
        if (providerId == QStringLiteral("gmail")) {
            if (account.value("oauthClientId").toString().trimmed().isEmpty()) {
                account.insert("oauthClientId", QStringLiteral(""));
                migrated = true;
            }
            if (account.value("oauthClientSecret").toString().isEmpty()) {
                account.insert("oauthClientSecret", QStringLiteral(""));
                migrated = true;
            }
        }
        m_accounts.push_back(account);
    }

    if (migrated) {
        save();
    }
}

void AccountRepository::save() const
{
    QJsonArray arr;
    for (const auto &entry : m_accounts) {
        arr.push_back(QJsonObject::fromVariantMap(entry.toMap()));
    }
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}
