#pragma once

#include <QVariantList>

class AccountRepository : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
public:
    explicit AccountRepository(QObject *parent = nullptr);

    Q_INVOKABLE void addOrUpdateAccount(const QVariantMap &account);
    Q_INVOKABLE bool removeAccount(const QString &email);

    [[nodiscard]] QVariantList accounts() const;

signals:
    void accountsChanged();

private:
    QVariantList m_accounts;
    QString m_path;

    void load();
    void save() const;
};
