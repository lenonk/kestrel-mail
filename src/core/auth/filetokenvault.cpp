#include "filetokenvault.h"

#include "../utils.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace Qt::Literals::StringLiterals;

FileTokenVault::FileTokenVault()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + "/kestrel-mail"_L1;
    QDir().mkpath(base);
    m_path = base + "/tokens.json"_L1;
}

bool FileTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken)
{
    QJsonObject obj;
    QFile in(m_path);
    if (in.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(in.readAll()).object();
    }

    obj[Kestrel::normalizeEmail(accountEmail)] = refreshToken;

    QFile out(m_path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString FileTokenVault::loadRefreshToken(const QString &accountEmail)
{
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject obj = QJsonDocument::fromJson(in.readAll()).object();
    return obj.value(Kestrel::normalizeEmail(accountEmail)).toString();
}

bool FileTokenVault::removeRefreshToken(const QString &accountEmail)
{
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly)) {
        return true;
    }

    QJsonObject obj = QJsonDocument::fromJson(in.readAll()).object();
    in.close();

    obj.remove(Kestrel::normalizeEmail(accountEmail));

    QFile out(m_path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}
