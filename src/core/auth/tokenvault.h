#pragma once

#include <QString>
#include <QVariantMap>

class TokenVault
{
public:
    TokenVault() = default;
    virtual ~TokenVault() = default;

    virtual bool storeRefreshToken(const QString &accountEmail, const QString &refreshToken) = 0;
    virtual QString loadRefreshToken(const QString &accountEmail) = 0;
    virtual bool removeRefreshToken(const QString &accountEmail) = 0;

};
