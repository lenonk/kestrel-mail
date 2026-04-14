#pragma once

#include <QObject>
#include <QVariantList>

class DataStore;

class AccountRepository : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
public:
    explicit AccountRepository(DataStore *store, QObject *parent = nullptr);

    Q_INVOKABLE void addOrUpdateAccount(const QVariantMap &account);
    Q_INVOKABLE bool removeAccount(const QString &email);

    [[nodiscard]] QVariantList accounts() const;

signals:
    void accountsChanged();

private:
    DataStore *m_store;
    QVariantList m_accounts;

    void load();
    void loadFromJson();
    void migrateFromJson();
};
