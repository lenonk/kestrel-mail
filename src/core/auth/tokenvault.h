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

    virtual bool storePassword(const QString &accountEmail, const QString &password) = 0;
    virtual QString loadPassword(const QString &accountEmail) = 0;
    virtual bool removePassword(const QString &accountEmail) = 0;

};
