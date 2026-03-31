#pragma once

#include <QString>

#include "tokenvault.h"

class LibSecretTokenVault : public TokenVault
{
public:
    LibSecretTokenVault();

    bool storeRefreshToken(const QString &accountEmail, const QString &refreshToken) override;
    QString loadRefreshToken(const QString &accountEmail) override;
    bool removeRefreshToken(const QString &accountEmail) override;
};
