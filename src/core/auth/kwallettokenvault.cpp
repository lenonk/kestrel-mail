#include "kwallettokenvault.h"

#include <KWallet>
#include <QEventLoop>

using namespace Qt::Literals::StringLiterals;

KWalletTokenVault::KWalletTokenVault() = default;

static KWallet::Wallet *openWallet()
{
    auto *wallet = KWallet::Wallet::openWallet(
        KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet)
        return nullptr;

    if (!wallet->hasFolder(QStringLiteral("kestrel-mail")))
        wallet->createFolder(QStringLiteral("kestrel-mail"));
    wallet->setFolder(QStringLiteral("kestrel-mail"));
    return wallet;
}

bool KWalletTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return false;
    return wallet->writePassword(accountEmail.trimmed().toLower(), refreshToken) == 0;
}

QString KWalletTokenVault::loadRefreshToken(const QString &accountEmail)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return {};
    QString token;
    if (wallet->readPassword(accountEmail.trimmed().toLower(), token) != 0)
        return {};
    return token;
}

bool KWalletTokenVault::removeRefreshToken(const QString &accountEmail)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return false;
    return wallet->removeEntry(accountEmail.trimmed().toLower()) == 0;
}
