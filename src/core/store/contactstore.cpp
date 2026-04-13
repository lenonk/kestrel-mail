#include "contactstore.h"
#include "../utils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>

#include <limits>
#include <mutex>

using namespace Qt::Literals::StringLiterals;

namespace {

const QRegularExpression kReNonAlnumSplit("[^a-z0-9]+"_L1);
const QRegularExpression kReHasLetters("[A-Za-z]"_L1);
const QRegularExpression kReCsvSemicolonOutsideQuotes(R"([,;](?=(?:[^"]*"[^"]*")*[^"]*$))"_L1);
const QRegularExpression kReEmailAddress(
    R"(([A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}))"_L1,
    QRegularExpression::CaseInsensitiveOption);

} // namespace

// ─── Construction ────────────────────────────────────────────────

ContactStore::ContactStore(DbAccessor dbAccessor, QThread *ownerThread)
    : m_db(std::move(dbAccessor))
    , m_ownerThread(ownerThread) { }

// ─── Static helpers ──────────────────────────────────────────────

QString
ContactStore::extractFirstEmail(const QString &raw) {
    const auto m = kReEmailAddress.match(raw);
    return m.hasMatch() ? Kestrel::normalizeEmail(m.captured(1)) : QString();
}

qint32
ContactStore::displayNameScoreForEmail(const QString &nameRaw, const QString &emailRaw) {
    const auto name = nameRaw.trimmed();
    const auto email = Kestrel::normalizeEmail(emailRaw);

    if (name.isEmpty()) { return std::numeric_limits<int>::min() / 4; }

    qint32 score = 0;
    const auto lower = name.toLower();

    if (name.contains(' ')) { score += 3; }
    if (name.size() >= 4 && name.size() <= 40) { score += 2; }

    const auto nameTokens = lower.split(kReNonAlnumSplit, Qt::SkipEmptyParts);
    if (nameTokens.size() >= 2) { score += 4; }

    if (!email.isEmpty()) {
        const auto at = static_cast<int>(email.indexOf('@'));
        const auto local = (at > 0) ? email.left(at) : email;

        for (const auto localTokens = local.split(kReNonAlnumSplit, Qt::SkipEmptyParts); const auto &tok : localTokens) {
            if (tok.size() < 3) { continue; }
            if (nameTokens.contains(tok)) { score += 8; }
        }

        if (lower.contains(local) && local.size() >= 4) { score += 6; }
    }

    static const QSet<QString> generic = {
        "microsoft"_L1, "outlook"_L1, "gmail"_L1,
        "team"_L1, "support"_L1, "customer"_L1,
        "service"_L1, "notification"_L1, "admin"_L1,
        "info"_L1, "noreply"_L1, "no"_L1
    };

    for (const auto &tok : nameTokens) {
        if (generic.contains(tok)) { score -= 4; }
    }

    if (name.contains('.')) { score -= 4; }
    if (name.contains('_')) { score -= 4; }
    if (name.contains('-')) { score -= 3; }

    const auto hasLetters = name.contains(kReHasLetters);
    if (hasLetters && name == name.toUpper() && name.size() > 3) { score -= 3; }
    if (hasLetters && name == name.toLower() && name.size() > 3) { score -= 3; }

    return score;
}

QString
ContactStore::extractExplicitDisplayName(const QString &raw, const QString &knownEmail) {
    auto s = raw.trimmed();

    if (s.isEmpty()) { return {}; }

    const auto email = Kestrel::normalizeEmail(knownEmail);
    if (!email.isEmpty() && s.compare(email, Qt::CaseInsensitive) == 0) {
        return {};
    }

    // If this is a mailbox list, pick the segment most likely associated with knownEmail.
    // Skip the split when the string contains only one email — a bare comma in the
    // display name (e.g. "Fried, Garret <x@y>") is not an address separator.
    if (s.count('@') > 1) {
        if (const auto parts = s.split(kReCsvSemicolonOutsideQuotes, Qt::SkipEmptyParts); parts.size() > 1) {
            QString best;
            qint32 bestScore = std::numeric_limits<int>::min() / 4;
            for (const auto &pRaw : parts) {
                const auto p = pRaw.trimmed();
                if (p.isEmpty()) { continue; }

                const auto pEmail = extractFirstEmail(p);
                if (!email.isEmpty() && !pEmail.isEmpty() && pEmail != email) { continue; }

                QString candidate = p;
                if (const auto lt2 = static_cast<int>(candidate.indexOf('<')); lt2 > 0) {
                    candidate = candidate.left(lt2).trimmed();
                }

                candidate.remove('"');
                candidate.remove('\'');
                candidate = candidate.trimmed();

                if (candidate.isEmpty()) { continue; }

                if (kReEmailAddress.match(candidate).hasMatch()) { continue; }

                const auto scoreEmail = email.isEmpty() ? pEmail : email;

                if (!scoreEmail.isEmpty()) {
                    const auto at = static_cast<int>(scoreEmail.indexOf('@'));
                    const auto local = (at > 0) ? scoreEmail.left(at) : scoreEmail;
                    if (!local.isEmpty() && candidate.compare(local, Qt::CaseInsensitive) == 0) { continue; }
                }

                if (const auto sc = displayNameScoreForEmail(candidate, scoreEmail); sc > bestScore) {
                    bestScore = sc; best = candidate;
                }
            }
            if (!best.isEmpty()) { return best; }
        }
    }

    if (const auto lt = static_cast<int>(s.indexOf('<')); lt > 0) {
        s = s.left(lt).trimmed();
    } else {
        if (!s.contains(' ')) { return {}; }
    }

    s.remove('"');
    s.remove('\'');
    s = s.trimmed();
    if (s.isEmpty()) { return {}; }

    if (kReEmailAddress.match(s).hasMatch()) { return {}; }

    if (!email.isEmpty()) {
        if (s.compare(email, Qt::CaseInsensitive) == 0) { return {}; }

        const auto at = static_cast<int>(email.indexOf('@'));
        const QString local = (at > 0) ? email.left(at) : email;

        if (!local.isEmpty() && s.compare(local, Qt::CaseInsensitive) == 0) {
            return {};
        }
    }
    return s;
}

// ─── Avatar queries ──────────────────────────────────────────────

QString
ContactStore::avatarForEmail(const QString &email) const {
    if (QThread::currentThread() != m_ownerThread) { return {}; }

    const auto e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) { return {}; }

    {
        QMutexLocker lock(&m_avatarCacheMutex);
        if (m_avatarCache.contains(e))
            return m_avatarCache.value(e);
    }

    if (const auto database = m_db(); database.isValid() && database.isOpen()) {
        QSqlQuery q(database);
        q.prepare("SELECT avatar_url FROM contact_avatars WHERE email=:email LIMIT 1"_L1);
        q.bindValue(":email"_L1, e);
        if (q.exec() && q.next()) {
            const auto cached = q.value(0).toString().trimmed();
            QMutexLocker lock(&m_avatarCacheMutex);
            m_avatarCache.insert(e, cached);
            return cached;
        }
    }

    QMutexLocker lock(&m_avatarCacheMutex);
    m_avatarCache.insert(e, {});
    return {};
}

bool
ContactStore::avatarShouldRefresh(const QString &email, int ttlSeconds, const int maxFailures) const {
    if (QThread::currentThread() != m_ownerThread) { return true; }

    const auto e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) { return false; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return true; }

    QSqlQuery q(database);
    q.prepare("SELECT avatar_url, last_checked_at, failure_count FROM contact_avatars WHERE email=:email LIMIT 1"_L1);
    q.bindValue(":email"_L1, e);

    if (!q.exec() || !q.next()) { return true; }

    const auto avatarUrl    = q.value(0).toString().trimmed();
    const auto checked      = q.value(1).toString().trimmed();
    const qint32 failures   = q.value(2).toInt();

    if (avatarUrl.startsWith("file://"_L1)) { return false; }

    const auto checkedAt = QDateTime::fromString(checked, Qt::ISODate);
    const auto now = QDateTime::currentDateTimeUtc();
    const qint64 age = checkedAt.isValid() ? checkedAt.secsTo(now) : (ttlSeconds + 1);

    if (failures >= maxFailures) {
        const int backoff = ttlSeconds * qMin(failures, 8);
        return age >= backoff;
    }

    return age >= ttlSeconds;
}

QStringList
ContactStore::staleGooglePeopleEmails(const int limit) const {
    if (QThread::currentThread() != m_ownerThread) { return {}; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    QSqlQuery q(database);
    q.prepare(R"(
        SELECT email FROM contact_avatars
        WHERE source='google-people'
          AND (length(trim(avatar_url)) = 0
               OR avatar_url LIKE 'https://%'
               OR avatar_url LIKE 'http://%')
          AND (last_checked_at IS NULL
               OR datetime(last_checked_at) < datetime('now', '-1 hour'))
        ORDER BY last_checked_at ASC
        LIMIT :lim
    )"_L1);

    q.bindValue(":lim"_L1, limit);
    if (!q.exec()) { return {}; }

    QStringList result;
    while (q.next()) {
        result << Kestrel::normalizeEmail(q.value(0).toString());
    }

    return result;
}

void
ContactStore::updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source) const {
    const auto database = m_db();

    if (!database.isValid() || !database.isOpen()) { return; }

    const auto e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) { return; }

    const auto storedUrl = avatarUrl.trimmed();

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
        VALUES (:email, :avatar_url, :source, datetime('now'), 0)
        ON CONFLICT(email) DO UPDATE SET
          avatar_url=excluded.avatar_url,
          source=excluded.source,
          last_checked_at=datetime('now'),
          failure_count=0
    )"_L1);

    q.bindValue(":email"_L1, e);
    q.bindValue(":avatar_url"_L1, storedUrl);
    q.bindValue(":source"_L1, source.trimmed().isEmpty()
                ? "google-people"_L1 : source.trimmed().toLower());
    q.exec();

    { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(e, storedUrl); }
}

// ─── Display-name queries ────────────────────────────────────────

QString
ContactStore::displayNameForEmail(const QString &email) const {
    if (QThread::currentThread() != m_ownerThread) { return {}; }

    const auto e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) { return {}; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) return {};

    QSqlQuery q(database);
    q.prepare("SELECT display_name FROM contact_display_names WHERE email=:email LIMIT 1"_L1);
    q.bindValue(":email"_L1, e);

    if (!q.exec() || !q.next()) { return {}; }

    return q.value(0).toString().trimmed();
}

QString
ContactStore::preferredSelfDisplayName(const QString &accountEmail) const {
    if (QThread::currentThread() != m_ownerThread) { return {}; }

    const auto e = Kestrel::normalizeEmail(accountEmail);
    if (e.isEmpty()) { return {}; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    QHash<QString, qint32> scores;
    auto consider = [&](const QString &raw, const qint32 weight) {
        const auto name = extractExplicitDisplayName(raw, e).trimmed();

        if (name.isEmpty()) { return; }
        if (name.compare(e, Qt::CaseInsensitive) == 0) { return; }

        qint32 s = weight;
        if (name == name.toUpper() && name.size() > 3) { s -= 2; }
        if (name.contains(' ')) { s += 2; }

        scores[name] += s;
    };

    QSqlQuery q(database);
    q.prepare("SELECT sender, recipient FROM messages WHERE lower(sender) LIKE :pat OR lower(recipient) LIKE :pat"_L1);

    q.bindValue(":pat"_L1, "%<"_L1 + e + ">%"_L1);
    if (q.exec()) {
        while (q.next()) {
            consider(q.value(0).toString(), 3);
            consider(q.value(1).toString(), 1);
        }
    }

    QString best;
    qint32 bestScore = std::numeric_limits<int>::min();
    for (auto it = scores.cbegin(); it != scores.cend(); ++it) {
        if (it.value() > bestScore) {
            bestScore = it.value();
            best = it.key();
        }
    }

    return best.trimmed();
}

QVariantList
ContactStore::searchContacts(const QString &prefix, const int limit) const {
    QVariantList out;
    if (QThread::currentThread() != m_ownerThread) { return out; }

    const auto p = prefix.trimmed();
    if (p.isEmpty()) { return out; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

    const auto pattern = p + QLatin1Char('%');
    QSqlQuery q(database);
    q.prepare(R"(
        SELECT email, display_name FROM contact_display_names
        WHERE email LIKE :pat OR display_name LIKE :pat2
        ORDER BY display_score DESC, last_seen_at DESC
        LIMIT :lim
    )"_L1);

    q.bindValue(":pat"_L1,  pattern);
    q.bindValue(":pat2"_L1, pattern);
    q.bindValue(":lim"_L1,  limit);

    if (!q.exec()) { return out; }

    while (q.next()) {
        QVariantMap row;
        row.insert("email"_L1,       q.value(0).toString());
        row.insert("displayName"_L1, q.value(1).toString().trimmed());
        out.append(row);
    }

    return out;
}

// ─── upsertHeader support (called from DataStore, any thread) ────

void
ContactStore::persistSenderAvatar(const QString &email, const QString &avatarUrl, const QString &source) const {
    if (email.isEmpty()) { return; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    if (!avatarUrl.isEmpty()) {
        // Hit: sender has a resolved avatar URL.
        QSqlQuery q(database);
        q.prepare(R"(
            INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
            VALUES (:email, :avatar_url, :source, datetime('now'), 0)
            ON CONFLICT(email) DO UPDATE SET
              avatar_url=excluded.avatar_url,
              source=CASE WHEN excluded.source IS NOT NULL AND length(trim(excluded.source))>0
                          THEN excluded.source ELSE contact_avatars.source END,
              last_checked_at=datetime('now'),
              failure_count=0
        )"_L1);

        q.bindValue(":email"_L1, email);
        q.bindValue(":avatar_url"_L1, avatarUrl);
        q.bindValue(":source"_L1, source);

        q.exec();
        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(email, avatarUrl); }
    } else {
        // Miss: no avatar resolved — record miss, preserve existing file:// URLs.
        QSqlQuery q(database);
        q.prepare(R"(
            INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
            VALUES (:email, '', 'lookup-miss', datetime('now'), 1)
            ON CONFLICT(email) DO UPDATE SET
              avatar_url=CASE
                           WHEN contact_avatars.avatar_url LIKE 'file://%'
                           THEN contact_avatars.avatar_url
                           ELSE ''
                         END,
              source=CASE
                       WHEN contact_avatars.avatar_url LIKE 'file://%'
                       THEN contact_avatars.source
                       ELSE 'lookup-miss'
                     END,
              last_checked_at=datetime('now'),
              failure_count=contact_avatars.failure_count + 1
        )"_L1);

        q.bindValue(":email"_L1, email);
        q.exec();

        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(email); }
    }
}

void
ContactStore::persistRecipientAvatar(const QString &email, const QString &avatarUrl, bool lookupMiss) const {
    if (email.isEmpty()) { return; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    if (!avatarUrl.isEmpty()) {
        // Hit: recipient has a resolved avatar URL.
        QSqlQuery q(database);
        q.prepare(R"(
            INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
            VALUES (:email, :avatar_url, 'google-people', datetime('now'), 0)
            ON CONFLICT(email) DO UPDATE SET
              avatar_url=excluded.avatar_url,
              source='google-people',
              last_checked_at=datetime('now'),
              failure_count=0
        )"_L1);

        q.bindValue(":email"_L1, email);
        q.bindValue(":avatar_url"_L1, avatarUrl);
        q.exec();

        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(email, avatarUrl); }
    } else if (lookupMiss) {
        // Miss: preserve existing google-people URLs if present.
        QSqlQuery q(database);
        q.prepare(R"(
            INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
            VALUES (:email, '', 'google-people-miss', datetime('now'), 1)
            ON CONFLICT(email) DO UPDATE SET
              avatar_url=CASE
                           WHEN contact_avatars.source='google-people'
                                AND length(trim(contact_avatars.avatar_url)) > 0
                           THEN contact_avatars.avatar_url
                           ELSE ''
                         END,
              source=CASE
                       WHEN contact_avatars.source='google-people'
                            AND length(trim(contact_avatars.avatar_url)) > 0
                       THEN contact_avatars.source
                       ELSE 'google-people-miss'
                     END,
              last_checked_at=datetime('now'),
              failure_count=CASE
                              WHEN contact_avatars.source='google-people'
                                   AND length(trim(contact_avatars.avatar_url)) > 0
                              THEN contact_avatars.failure_count
                              ELSE contact_avatars.failure_count + 1
                            END
        )"_L1);

        q.bindValue(":email"_L1, email);
        q.exec();

        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(email); }
    }
}

void
ContactStore::persistDisplayName(const QString &email, const QString &displayName, const QString &source) const {
    const auto e = Kestrel::normalizeEmail(email);
    const auto cand = displayName.trimmed();

    if (e.isEmpty() || cand.isEmpty()) { return; }

    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QString existing;
    qint32 existingScore = std::numeric_limits<int>::min() / 4;

    {
        QSqlQuery qCur(database);
        qCur.prepare("SELECT display_name, display_score FROM contact_display_names WHERE email=:email LIMIT 1"_L1);
        qCur.bindValue(":email"_L1, e);

        if (qCur.exec() && qCur.next()) {
            existing = qCur.value(0).toString().trimmed();
            existingScore = qCur.value(1).toInt();
        }
    }

    const auto newScore = displayNameScoreForEmail(cand, e);
    const auto oldScore = !existing.isEmpty() ? existingScore : (std::numeric_limits<int>::min() / 4);
    if (!existing.isEmpty() && newScore < oldScore) { return; }

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO contact_display_names (email, display_name, source, display_score, last_seen_at)
        VALUES (:email, :display_name, :source, :display_score, datetime('now'))
        ON CONFLICT(email) DO UPDATE SET
          display_name=excluded.display_name,
          display_score=excluded.display_score,
          source=CASE
                   WHEN excluded.source IS NOT NULL AND length(trim(excluded.source)) > 0
                   THEN excluded.source
                   ELSE contact_display_names.source
                 END,
          last_seen_at=datetime('now')
    )"_L1);

    q.bindValue(":email"_L1, e);
    q.bindValue(":display_name"_L1, cand);
    q.bindValue(":source"_L1, source);
    q.bindValue(":display_score"_L1, newScore);
    q.exec();
}

// ─── Avatar disk cache ───────────────────────────────────────────

QString
ContactStore::avatarDirPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + "/kestrel-mail/avatars"_L1;
}

QString
ContactStore::writeAvatarDataUri(const QString &email, const QString &dataUri) {
    static const QRegularExpression re(R"(^data:(image/[^;,]+);base64,(.+)$)"_L1,
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(dataUri.trimmed());
    if (!m.hasMatch()) { return {}; }

    const auto mime = m.captured(1).trimmed().toLower();
    const auto bytes = QByteArray::fromBase64(m.captured(2).trimmed().toLatin1());
    if (bytes.isEmpty()) { return {}; }

    QString ext = "bin"_L1;
    if (mime.startsWith("image/png"_L1))        { ext = "png"_L1; }
    else if (mime.contains("jpeg"_L1) ||
        mime.contains("jpg"_L1))                { ext = "jpg"_L1; }
    else if (mime.startsWith("image/webp"_L1))  { ext = "webp"_L1; }
    else if (mime.startsWith("image/gif"_L1))   { ext = "gif"_L1; }
    else if (mime.startsWith("image/svg"_L1))   { ext = "svg"_L1; }

    const auto dir = avatarDirPath();
    static std::once_flag mkdirFlag;
    std::call_once(mkdirFlag, [&] { QDir().mkpath(dir); });

    const auto hash = QString::fromLatin1(
        QCryptographicHash::hash(Kestrel::normalizeEmail(email).toUtf8(), QCryptographicHash::Sha1).toHex());
    const auto absPath = dir + "/"_L1 + hash + "."_L1 + ext;

    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly)) { return {}; }

    f.write(bytes);
    f.close();

    return "file://"_L1 + absPath;
}

// ─── Static avatar rendering ─────────────────────────────────────

QString
ContactStore::avatarInitials(const QString &displayName, const QString &fallback) {
    const auto raw = displayName.trimmed().isEmpty() ? fallback.trimmed() : displayName.trimmed();
    if (raw.isEmpty()) { return "?"_L1; }

    static const QRegularExpression reWs(R"(\s+)"_L1);
    const auto parts = raw.split(reWs, Qt::SkipEmptyParts);
    QString initials;

    for (const auto &p : parts) {
        if (initials.size() >= 2) { break; }
        if (!p.isEmpty()) { initials += p.at(0).toUpper(); }
    }

    return initials.isEmpty() ? raw.left(1).toUpper() : initials;
}

QColor
ContactStore::avatarColor(const QString &displayName, const QString &fallback) {
    const auto key = (displayName + "|"_L1 + fallback).trimmed().toLower();
    const auto input = (key.isEmpty() ? "unknown"_L1 : key).toUtf8();

    quint32 h = 2166136261u;
    for (const char c : input) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }

    const float hue = static_cast<float>(h % 360) / 360.0f;
    return QColor::fromHslF(hue, 0.50f, 0.45f, 1.0f);
}

QPixmap
ContactStore::avatarPixmap(const QString &displayName, const QString &email, const qint32 size) {
    const auto initials = avatarInitials(displayName, email);
    const auto bg = avatarColor(displayName, email);

    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    p.setFont(QFont("sans-serif"_L1, size / 3, QFont::Bold));
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initials);
    p.end();
    return px;
}
