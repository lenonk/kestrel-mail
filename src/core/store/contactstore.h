#pragma once

#include <QColor>
#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QThread;

/// Owns all contact-avatar and display-name persistence / queries.
/// Thread-safe: uses the same per-thread SQLite connection pool as DataStore.
class ContactStore
{
public:
    using DbAccessor = std::function<QSqlDatabase()>;

    explicit ContactStore(DbAccessor dbAccessor, QThread *ownerThread);

    // ── Avatar queries (UI-thread only) ──────────────────────────
    QString avatarForEmail(const QString &email) const;
    bool avatarShouldRefresh(const QString &email, int ttlSeconds = 3600, int maxFailures = 3) const;
    QStringList staleGooglePeopleEmails(int limit = 20) const;
    void updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source) const;

    // ── Display-name queries (UI-thread only) ────────────────────
    QString displayNameForEmail(const QString &email) const;
    QString preferredSelfDisplayName(const QString &accountEmail) const;
    QVariantList searchContacts(const QString &prefix, int limit = 10) const;

    // ── Called from upsertHeader (any thread) ────────────────────
    void persistSenderAvatar(const QString &email, const QString &avatarUrl, const QString &source) const;
    void persistRecipientAvatar(const QString &email, const QString &avatarUrl, bool lookupMiss) const;
    void persistDisplayName(const QString &email, const QString &displayName, const QString &source) const;

    // ── Avatar disk-cache helpers ────────────────────────────────
    static QString avatarDirPath();

    static QString writeAvatarDataUri(const QString &email, const QString &dataUri);

    // ── Static / pure-computation helpers ────────────────────────
    static QString avatarInitials(const QString &displayName, const QString &fallback);
    static QColor  avatarColor(const QString &displayName, const QString &fallback);
    static QPixmap avatarPixmap(const QString &displayName, const QString &email, int size = 64);

    /// Scores a candidate display name for how "real" it looks relative to the email.
    static int displayNameScoreForEmail(const QString &nameRaw, const QString &emailRaw);

    /// Extracts a human display name from an RFC-5322 mailbox string.
    static QString extractExplicitDisplayName(const QString &raw, const QString &knownEmail);

    /// Extracts the first email address from a raw header value.
    static QString extractFirstEmail(const QString &raw);

private:
    DbAccessor m_db;
    QThread *m_ownerThread = nullptr;

    mutable QMutex m_avatarCacheMutex;
    mutable QHash<QString, QString> m_avatarCache;
};
