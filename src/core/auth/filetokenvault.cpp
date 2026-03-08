#include "filetokenvault.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

FileTokenVault::FileTokenVault()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/kestrel-mail");
    QDir().mkpath(base);
    m_path = base + QStringLiteral("/tokens.json");
}

bool FileTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken)
{
    QJsonObject obj;
    QFile in(m_path);
    if (in.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(in.readAll()).object();
    }

    obj[accountEmail.trimmed().toLower()] = refreshToken;

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
    return obj.value(accountEmail.trimmed().toLower()).toString();
}

bool FileTokenVault::removeRefreshToken(const QString &accountEmail)
{
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly)) {
        return true;
    }

    QJsonObject obj = QJsonDocument::fromJson(in.readAll()).object();
    in.close();

    obj.remove(accountEmail.trimmed().toLower());

    QFile out(m_path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}
