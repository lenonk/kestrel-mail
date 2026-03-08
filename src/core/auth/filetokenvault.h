#pragma once

#include <QString>

#include "tokenvault.h"

class FileTokenVault : public TokenVault
{
public:
    FileTokenVault();

    bool storeRefreshToken(const QString &accountEmail, const QString &refreshToken) override;
    QString loadRefreshToken(const QString &accountEmail) override;
    bool removeRefreshToken(const QString &accountEmail) override;

private:
    QString m_path;
};
