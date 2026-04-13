#pragma once

#include <QString>

#include "tokenvault.h"

class KWalletTokenVault : public TokenVault {
public:
    KWalletTokenVault();

    bool storeRefreshToken(const QString &accountEmail, const QString &refreshToken) override;
    QString loadRefreshToken(const QString &accountEmail) override;
    bool removeRefreshToken(const QString &accountEmail) override;

    bool storePassword(const QString &accountEmail, const QString &password) override;
    QString loadPassword(const QString &accountEmail) override;
    bool removePassword(const QString &accountEmail) override;

private:
    static constexpr auto kWalletFolder = "kestrel-mail";
};
