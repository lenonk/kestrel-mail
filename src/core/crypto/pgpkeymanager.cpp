#include "pgpkeymanager.h"
#include "../store/datastore.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtConcurrent>

#include <botan/auto_rng.h>
#include <botan/hash.h>
#include <botan/pkcs8.h>
#include <botan/rsa.h>
#include <botan/x509_key.h>

using namespace Qt::Literals::StringLiterals;

static QString keysBasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + "/kestrel-mail/keys"_L1;
}

PgpKeyManager::PgpKeyManager(DataStore *dataStore, QObject *parent)
    : QObject(parent), m_dataStore(dataStore) {}

QString
PgpKeyManager::fingerprint() const { return m_fingerprint; }

QString
PgpKeyManager::publicKeyArmored() const { return m_publicKeyPem; }

bool
PgpKeyManager::generating() const { return m_generating; }

void
PgpKeyManager::generateKeypair(const QString &email, const QString &password, const int keySize)
{
    if (m_generating) return;
    m_generating = true;
    emit generatingChanged();

    (void)QtConcurrent::run([this, email, password, keySize]() {
        const auto result = doGenerate(email, password, keySize);
        QMetaObject::invokeMethod(this, [this, email, result]() {
            if (result.ok) {
                m_fingerprint = result.fingerprint;
                m_publicKeyPem = result.publicKeyPem;
                m_privateKeyPemEnc = result.privateKeyPemEnc;
                storeKeys(email, result);
            }
            m_generating = false;
            emit generatingChanged();
            emit keypairGenerated(result.ok, result.errorMessage);
        }, Qt::QueuedConnection);
    });
}

PgpKeyManager::KeypairResult
PgpKeyManager::doGenerate(const QString & /*email*/, const QString &password, const int keySize)
{
    KeypairResult r;
    r.keySize = keySize;

    try {
        Botan::AutoSeeded_RNG rng;
        const Botan::RSA_PrivateKey privKey(rng, keySize);

        // Fingerprint: SHA-256 of the public key DER encoding.
        const auto pubDer = Botan::X509::BER_encode(privKey);
        auto sha256 = Botan::HashFunction::create("SHA-256");
        sha256->update(pubDer);
        const auto hash = sha256->final();

        QString fp;
        for (size_t i = 0; i < hash.size(); ++i) {
            if (i > 0) fp += ':';
            fp += QStringLiteral("%1").arg(hash[i], 2, 16, QLatin1Char('0')).toUpper();
        }
        r.fingerprint = fp;

        // Public key PEM.
        r.publicKeyPem = QString::fromStdString(Botan::X509::PEM_encode(privKey));

        // Private key PEM (password-encrypted for storage).
        r.privateKeyPemEnc = QString::fromStdString(
            Botan::PKCS8::PEM_encode(privKey, rng, password.toStdString()));

        r.ok = true;
    } catch (const std::exception &e) {
        r.errorMessage = QString::fromStdString(e.what());
    }

    return r;
}

bool
PgpKeyManager::storeKeys(const QString &email, const KeypairResult &result)
{
    const auto base = keysBasePath();
    QDir().mkpath(base);

    const auto fpClean = QString(result.fingerprint).remove(':');

    // Write public key.
    {
        QFile f(base + "/"_L1 + fpClean + ".pub.pem"_L1);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(result.publicKeyPem.toUtf8());
        }
    }

    // Write encrypted private key with owner-only permissions.
    {
        QFile f(base + "/"_L1 + fpClean + ".sec.pem"_L1);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(result.privateKeyPemEnc.toUtf8());
            f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    // Record in DB.
    if (m_dataStore)
        m_dataStore->insertPgpKey(email, result.fingerprint, result.keySize);

    return true;
}

bool
PgpKeyManager::exportPrivateKey(const QString &filePath) const
{
    if (m_privateKeyPemEnc.isEmpty() || filePath.isEmpty()) return false;

    auto path = filePath;
    if (path.startsWith("file://"_L1))
        path = QUrl(path).toLocalFile();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    f.write(m_privateKeyPemEnc.toUtf8());
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
