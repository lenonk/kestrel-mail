#include "libsecrettokenvault.h"

#include "../utils.h"

// GLib's gio headers use "signals" as a struct member, which collides with
// Qt's "signals" keyword macro.  Temporarily undefine it.
#undef signals
#include <libsecret/secret.h>
#include <glib.h>
#define signals Q_SIGNALS

static const SecretSchema kSchema = {
    "com.kestrelmail.RefreshToken",
    SECRET_SCHEMA_NONE,
    {
        { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
    },
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

LibSecretTokenVault::LibSecretTokenVault() = default;

bool LibSecretTokenVault::storeRefreshToken(const QString &accountEmail, const QString &refreshToken)
{
    const QByteArray account = Kestrel::normalizeEmail(accountEmail).toUtf8();
    const QByteArray token   = refreshToken.toUtf8();

    GError *error = nullptr;
    const gboolean ok = secret_password_store_sync(
        &kSchema,
        SECRET_COLLECTION_DEFAULT,
        "Kestrel Mail refresh token",
        token.constData(),
        nullptr,
        &error,
        "account", account.constData(),
        nullptr);

    if (error) {
        qWarning("LibSecretTokenVault::store failed: %s", error->message);
        g_error_free(error);
    }
    return ok == TRUE;
}

QString LibSecretTokenVault::loadRefreshToken(const QString &accountEmail)
{
    const QByteArray account = Kestrel::normalizeEmail(accountEmail).toUtf8();

    GError *error = nullptr;
    gchar *password = secret_password_lookup_sync(
        &kSchema,
        nullptr,
        &error,
        "account", account.constData(),
        nullptr);

    if (error) {
        qWarning("LibSecretTokenVault::load failed: %s", error->message);
        g_error_free(error);
        return {};
    }

    if (!password)
        return {};

    const QString result = QString::fromUtf8(password);
    secret_password_free(password);
    return result;
}

bool LibSecretTokenVault::removeRefreshToken(const QString &accountEmail)
{
    const QByteArray account = Kestrel::normalizeEmail(accountEmail).toUtf8();

    GError *error = nullptr;
    const gboolean ok = secret_password_clear_sync(
        &kSchema,
        nullptr,
        &error,
        "account", account.constData(),
        nullptr);

    if (error) {
        qWarning("LibSecretTokenVault::remove failed: %s", error->message);
        g_error_free(error);
    }
    return ok == TRUE;
}
