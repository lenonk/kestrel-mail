#include "kwallettokenvault.h"

#include "../utils.h"

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

    if (!wallet->hasFolder("kestrel-mail"_L1))
        wallet->createFolder("kestrel-mail"_L1);
    wallet->setFolder("kestrel-mail"_L1);
    return wallet;
}

bool KWalletTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return false;
    return wallet->writePassword(Kestrel::normalizeEmail(accountEmail), refreshToken) == 0;
}

QString KWalletTokenVault::loadRefreshToken(const QString &accountEmail)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return {};
    QString token;
    if (wallet->readPassword(Kestrel::normalizeEmail(accountEmail), token) != 0)
        return {};
    return token;
}

bool KWalletTokenVault::removeRefreshToken(const QString &accountEmail)
{
    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet)
        return false;
    return wallet->removeEntry(Kestrel::normalizeEmail(accountEmail)) == 0;
}
