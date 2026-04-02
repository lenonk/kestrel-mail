#include "accountrepository.h"

#include "../utils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace Qt::Literals::StringLiterals;

AccountRepository::AccountRepository(QObject *parent)
    : QObject(parent) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kestrel-mail"_L1;
    // QDir().mkpath(base);
    m_path = base + "/accounts.json"_L1;
    load();
}

QVariantList
AccountRepository::accounts() const {
    return m_accounts;
}

void
AccountRepository::addOrUpdateAccount(const QVariantMap &account) {
    const auto email = Kestrel::normalizeEmail(account.value("email").toString());

    if (email.isEmpty()) {
        return;
    }

    auto updated = false;
    for (auto & m_account : m_accounts) {
        if (auto existing = m_account.toMap(); Kestrel::normalizeEmail(existing.value("email").toString()) == email) {
            for (auto it = account.constBegin(); it != account.constEnd(); ++it) {
                // Don't overwrite existing non-null values with null/empty
                if (it.value().isNull()) continue;
                existing.insert(it.key(), it.value());
            }

            m_account = existing;
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

bool
AccountRepository::removeAccount(const QString &email) {
    const auto normalized = Kestrel::normalizeEmail(email);
    if (normalized.isEmpty()) {
        return false;
    }

    for (int i = 0; i < m_accounts.size(); ++i) {
        const auto existing = m_accounts[i].toMap();
        if (Kestrel::normalizeEmail(existing.value("email").toString()) == normalized) {
            m_accounts.removeAt(i);
            save();
            emit accountsChanged();
            return true;
        }
    }

    return false;
}

void
AccountRepository::load() {
    QFile f(m_path);

    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }

    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto arr = doc.array();
    m_accounts.clear();

    auto migrated = false;
    for (const auto &v : arr) {
        auto account = v.toObject().toVariantMap();
        if (const auto providerId = account.value("providerId").toString(); providerId == "gmail"_L1) {
            if (account.value("oauthClientId").toString().trimmed().isEmpty()) {
                account.insert("oauthClientId", ""_L1);
                migrated = true;
            }
            if (account.value("oauthClientSecret").toString().isEmpty()) {
                account.insert("oauthClientSecret", ""_L1);
                migrated = true;
            }
        }
        m_accounts.push_back(account);
    }

    if (migrated) {
        save();
    }
}

void
AccountRepository::save() const {
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
