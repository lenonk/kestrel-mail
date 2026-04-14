#include "accountrepository.h"

#include "../store/datastore.h"
#include "../utils.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace Qt::Literals::StringLiterals;

AccountRepository::AccountRepository(DataStore *store, QObject *parent)
    : QObject(parent), m_store(store) {
    migrateFromJson();
    load();
}

QVariantList
AccountRepository::accounts() const {
    return m_accounts;
}

void
AccountRepository::addOrUpdateAccount(const QVariantMap &account) {
    const auto email = Kestrel::normalizeEmail(account.value("email"_L1).toString());
    if (email.isEmpty()) return;

    if (m_store)
        m_store->upsertAccount(account);

    // Update in-memory cache.
    auto updated = false;
    for (auto &entry : m_accounts) {
        if (auto existing = entry.toMap(); Kestrel::normalizeEmail(existing.value("email"_L1).toString()) == email) {
            for (auto it = account.constBegin(); it != account.constEnd(); ++it) {
                if (it.value().isNull()) continue;
                existing.insert(it.key(), it.value());
            }
            entry = existing;
            updated = true;
            break;
        }
    }
    if (!updated)
        m_accounts.push_back(account);

    emit accountsChanged();
}

bool
AccountRepository::removeAccount(const QString &email) {
    const auto normalized = Kestrel::normalizeEmail(email);
    if (normalized.isEmpty()) return false;

    if (m_store)
        m_store->deleteAccount(normalized);

    const auto removed = m_accounts.removeIf([&](const QVariant &v) {
        return Kestrel::normalizeEmail(v.toMap().value("email"_L1).toString()) == normalized;
    });

    if (removed > 0) {
        emit accountsChanged();
        return true;
    }
    return false;
}

void
AccountRepository::load() {
    m_accounts.clear();
    if (m_store)
        m_accounts = m_store->loadAccounts();
}

void
AccountRepository::migrateFromJson() {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/kestrel-mail"_L1;
    const auto jsonPath = base + "/accounts.json"_L1;

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    const auto arr = doc.array();
    if (arr.isEmpty()) return;

    // Check if DB already has accounts (skip migration if so).
    if (m_store && !m_store->loadAccounts().isEmpty()) {
        // DB already populated — rename JSON as backup.
        QFile::rename(jsonPath, jsonPath + ".migrated"_L1);
        return;
    }

    qInfo() << "[account-migration] Migrating" << arr.size() << "accounts from JSON to DB";

    for (const auto &v : arr) {
        const auto account = v.toObject().toVariantMap();
        if (m_store)
            m_store->upsertAccount(account);
    }

    // Rename the JSON file so we don't re-migrate.
    QFile::rename(jsonPath, jsonPath + ".migrated"_L1);
}
