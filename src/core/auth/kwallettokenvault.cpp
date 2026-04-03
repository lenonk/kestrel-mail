#include "kwallettokenvault.h"

#include "../utils.h"

#include <KWallet>
#include <QEventLoop>

using namespace Qt::Literals::StringLiterals;

static constexpr auto kWalletFolder = "kestrel-mail";

KWalletTokenVault::KWalletTokenVault() = default;

static KWallet::Wallet *
openWallet() {
    auto *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);

    if (!wallet) { return nullptr; }

    const auto folder = QString::fromLatin1(kWalletFolder);
    if (!wallet->hasFolder(folder)) { wallet->createFolder(folder); }
    wallet->setFolder(folder);

    return wallet;
}

bool
KWalletTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken) {
    const std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet) { return false; }

    return wallet->writePassword(Kestrel::normalizeEmail(accountEmail), refreshToken) == 0;
}

QString
KWalletTokenVault::loadRefreshToken(const QString &accountEmail) {
    const std::unique_ptr<KWallet::Wallet> wallet(openWallet());

    if (!wallet) { return {}; }

    QString token;
    if (wallet->readPassword(Kestrel::normalizeEmail(accountEmail), token) != 0) { return {}; }

    return token;
}

bool
KWalletTokenVault::removeRefreshToken(const QString &accountEmail) {
    const std::unique_ptr<KWallet::Wallet> wallet(openWallet());

    if (!wallet) { return false; }

    return wallet->removeEntry(Kestrel::normalizeEmail(accountEmail)) == 0;
}
