#pragma once

#include <QObject>
#include <QString>

class DataStore;

class PgpKeyManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY keypairGenerated)
    Q_PROPERTY(QString publicKeyArmored READ publicKeyArmored NOTIFY keypairGenerated)
    Q_PROPERTY(bool generating READ generating NOTIFY generatingChanged)

public:
    explicit PgpKeyManager(DataStore *dataStore, QObject *parent = nullptr);

    [[nodiscard]] QString fingerprint() const;
    [[nodiscard]] QString publicKeyArmored() const;
    [[nodiscard]] bool generating() const;

    Q_INVOKABLE void generateKeypair(const QString &email, const QString &password, int keySize);
    Q_INVOKABLE bool exportPrivateKey(const QString &filePath) const;

signals:
    void keypairGenerated(bool ok, const QString &errorMessage);
    void generatingChanged();

private:
    struct KeypairResult {
        bool ok = false;
        QString errorMessage;
        QString fingerprint;
        QString publicKeyPem;
        QString privateKeyPemEnc;
        int keySize = 0;
    };

    static KeypairResult doGenerate(const QString &email, const QString &password, int keySize);
    bool storeKeys(const QString &email, const KeypairResult &result);

    DataStore *m_dataStore = nullptr;
    QString m_fingerprint;
    QString m_publicKeyPem;
    QString m_privateKeyPemEnc;
    bool m_generating = false;
};
