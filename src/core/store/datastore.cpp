#include "datastore.h"

#include <QDateTime>
#include <QLocale>
#include <QSqlDatabase>
#include <algorithm>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QColor>
#include <limits>

#include "src/core/transport/imap/sync/kestreltimer.h"

using namespace Qt::Literals::StringLiterals;

// Extract the first RFC 5322 Message-ID from a header value (e.g. References, In-Reply-To).
// Returns a normalized (lowercase, no angle-brackets) form.
static QString extractFirstMessageIdFromHeader(const QString &val)
{
    if (val.trimmed().isEmpty()) return {};
    static const QRegularExpression re(QStringLiteral("<([^>\\s]+)>"));
    const auto m = re.match(val);
    if (m.hasMatch())
        return m.captured(1).trimmed().toLower();
    // Bare message-id without angle brackets — take first whitespace-delimited token.
    const QStringList parts = val.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.first().toLower();
}

// Compute the canonical thread root ID from the three RFC 5322 threading headers.
// References chain → In-Reply-To → own Message-ID.
static QString computeThreadId(const QString &refs, const QString &irt, const QString &ownMsgId)
{
    const QString fromRefs = extractFirstMessageIdFromHeader(refs);
    if (!fromRefs.isEmpty()) return fromRefs;
    const QString fromIrt  = extractFirstMessageIdFromHeader(irt);
    if (!fromIrt.isEmpty())  return fromIrt;
    return extractFirstMessageIdFromHeader(ownMsgId);
}

QString extractFirstEmail(const QString &raw)
{
    const QRegularExpression re(QStringLiteral("([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(raw);
    return m.hasMatch() ? m.captured(1).trimmed().toLower() : QString();
}

static bool computeHasTrackingPixel(const QString &bodyHtml, const QString &senderRaw)
{
    if (bodyHtml.isEmpty())
        return false;

    const QString email = extractFirstEmail(senderRaw);
    QString senderDomain;
    {
        const int at = email.lastIndexOf('@');
        if (at >= 0) {
            senderDomain = email.mid(at + 1).toLower().trimmed();
            const int colon = senderDomain.indexOf(':');
            if (colon > 0) senderDomain = senderDomain.left(colon);
        }
    }

    static const QRegularExpression imgTagRe(QStringLiteral("<img\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcRe(QStringLiteral("\\bsrc\\s*=\\s*[\"'](https?://[^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression widthRe(QStringLiteral("\\bwidth\\s*=\\s*[\"']\\s*1\\s*[\"']"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression heightRe(QStringLiteral("\\bheight\\s*=\\s*[\"']\\s*1\\s*[\"']"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hostRe(QStringLiteral("^https?://([^/?#]+)"), QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = imgTagRe.globalMatch(bodyHtml);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        if (!widthRe.match(tag).hasMatch() || !heightRe.match(tag).hasMatch())
            continue;
        const auto srcM = srcRe.match(tag);
        if (!srcM.hasMatch()) continue;
        const QString src = srcM.captured(1);
        const auto hostM = hostRe.match(src);
        if (!hostM.hasMatch()) continue;
        QString pixelHost = hostM.captured(1).toLower().trimmed();
        const int colon = pixelHost.indexOf(':');
        if (colon > 0) pixelHost = pixelHost.left(colon);
        if (!senderDomain.isEmpty() && !pixelHost.isEmpty()
                && (pixelHost == senderDomain || pixelHost.endsWith("." + senderDomain)))
            continue;
        return true;
    }
    return false;
}

int displayNameScoreForEmail(const QString &nameRaw, const QString &emailRaw)
{
    const QString name = nameRaw.trimmed();
    const QString email = emailRaw.trimmed().toLower();
    if (name.isEmpty()) return std::numeric_limits<int>::min() / 4;

    int score = 0;
    const QString lower = name.toLower();

    if (name.contains(' ')) score += 3;
    if (name.size() >= 4 && name.size() <= 40) score += 2;

    const QStringList nameTokens = lower.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
    if (nameTokens.size() >= 2) score += 4;

    if (!email.isEmpty()) {
        const int at = email.indexOf('@');
        const QString local = (at > 0) ? email.left(at) : email;
        const QStringList localTokens = local.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
        for (const QString &tok : localTokens) {
            if (tok.size() < 3) continue;
            if (nameTokens.contains(tok)) score += 8;
        }
        if (lower.contains(local) && local.size() >= 4) score += 6;
    }

    static const QSet<QString> generic = {
        QStringLiteral("microsoft"), QStringLiteral("outlook"), QStringLiteral("gmail"),
        QStringLiteral("team"), QStringLiteral("support"), QStringLiteral("customer"),
        QStringLiteral("service"), QStringLiteral("notification"), QStringLiteral("admin"),
        QStringLiteral("info"), QStringLiteral("noreply"), QStringLiteral("no")
    };
    for (const QString &tok : nameTokens) {
        if (generic.contains(tok)) score -= 4;
    }

    // Penalize handle-like punctuation in candidate display names.
    if (name.contains('.')) score -= 4;
    if (name.contains('_')) score -= 4;
    if (name.contains('-')) score -= 3;

    const bool hasLetters = name.contains(QRegularExpression(QStringLiteral("[A-Za-z]")));
    if (hasLetters && name == name.toUpper() && name.size() > 3) score -= 3;
    if (hasLetters && name == name.toLower() && name.size() > 3) score -= 3;

    return score;
}

QString extractExplicitDisplayName(const QString &raw, const QString &knownEmail)
{
    QString s = raw.trimmed();
    if (s.isEmpty()) return {};

    const QString email = knownEmail.trimmed().toLower();
    if (!email.isEmpty() && s.compare(email, Qt::CaseInsensitive) == 0) {
        return {};
    }

    // If this is a mailbox list, pick the segment most likely associated with knownEmail.
    const QStringList parts = s.split(QRegularExpression(QStringLiteral("[,;](?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)")), Qt::SkipEmptyParts);
    if (parts.size() > 1) {
        QString best;
        int bestScore = std::numeric_limits<int>::min() / 4;
        for (const QString &pRaw : parts) {
            const QString p = pRaw.trimmed();
            if (p.isEmpty()) continue;
            const QString pEmail = extractFirstEmail(p);
            if (!email.isEmpty() && !pEmail.isEmpty() && pEmail != email) continue;

            QString candidate = p;
            const int lt2 = candidate.indexOf('<');
            if (lt2 > 0) candidate = candidate.left(lt2).trimmed();
            candidate.remove('"');
            candidate.remove('\'');
            candidate = candidate.trimmed();
            if (candidate.isEmpty()) continue;
            static const QRegularExpression emailTokenRe(
                QStringLiteral("([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})"),
                QRegularExpression::CaseInsensitiveOption);
            if (emailTokenRe.match(candidate).hasMatch()) continue;

            const QString scoreEmail = email.isEmpty() ? pEmail : email;
            if (!scoreEmail.isEmpty()) {
                const int at = scoreEmail.indexOf('@');
                const QString local = (at > 0) ? scoreEmail.left(at) : scoreEmail;
                if (!local.isEmpty() && candidate.compare(local, Qt::CaseInsensitive) == 0) continue;
            }

            const int sc = displayNameScoreForEmail(candidate, scoreEmail);
            if (sc > bestScore) { bestScore = sc; best = candidate; }
        }
        if (!best.isEmpty()) return best;
    }

    const int lt = s.indexOf('<');
    if (lt > 0) {
        s = s.left(lt).trimmed();
    } else {
        if (!s.contains(' ')) return {};
    }

    s.remove('"');
    s.remove('\'');
    s = s.trimmed();
    if (s.isEmpty()) return {};

    static const QRegularExpression emailTokenRe(
        QStringLiteral("([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})"),
        QRegularExpression::CaseInsensitiveOption);
    if (emailTokenRe.match(s).hasMatch()) return {};

    if (!email.isEmpty()) {
        if (s.compare(email, Qt::CaseInsensitive) == 0) return {};
        const int at = email.indexOf('@');
        const QString local = (at > 0) ? email.left(at) : email;
        if (!local.isEmpty() && s.compare(local, Qt::CaseInsensitive) == 0) {
            // Hard fail: local-part mirror is not a valid display name candidate.
            return {};
        }
    }
    return s;
}

QString faviconUrlForEmail(const QString &email)
{
    const QString e = email.trimmed().toLower();
    const int at = e.indexOf('@');
    if (at <= 0 || at + 1 >= e.size()) return {};
    const QString full = e.mid(at + 1).trimmed();
    if (full.isEmpty()) return {};

    QString domain = full;
    const QStringList parts = full.split('.');
    if (parts.size() > 2) {
        const QString tail2 = parts.mid(parts.size() - 2).join('.');
        static const QSet<QString> cc2 = {
            QStringLiteral("co.uk"), QStringLiteral("com.au"), QStringLiteral("co.jp"),
            QStringLiteral("com.br"), QStringLiteral("com.mx")
        };
        if (cc2.contains(tail2) && parts.size() >= 3) {
            domain = parts.mid(parts.size() - 3).join('.');
        } else {
            domain = tail2;
        }
    }

    return QStringLiteral("https://www.google.com/s2/favicons?domain=%1&sz=128").arg(domain);
}

namespace {

int purgeCategoryFolderEdges(QSqlDatabase &database)
{
    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        DELETE FROM message_folder_map
        WHERE lower(folder) LIKE '%/categories/%'
    )"));
    if (!q.exec()) return -1;
    return q.numRowsAffected();
}

QPair<int,int> repairTagProjectionInvariants(QSqlDatabase &database)
{
    int insertedTags = 0;
    int insertedMaps = 0;

    QSqlQuery q1(database);
    q1.prepare(QStringLiteral(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        SELECT ml.label, lower(ml.label), 'server', datetime('now')
        FROM message_labels ml
        LEFT JOIN tags t ON t.normalized_name = lower(ml.label)
        WHERE t.id IS NULL
        GROUP BY lower(ml.label)
    )"));
    if (q1.exec()) insertedTags = q1.numRowsAffected();

    QSqlQuery q2(database);
    q2.prepare(QStringLiteral(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT ml.account_email, ml.message_id, t.id, 'server', datetime('now')
        FROM message_labels ml
        JOIN tags t ON t.normalized_name = lower(ml.label)
        LEFT JOIN message_tag_map mtm
          ON mtm.account_email = ml.account_email
         AND mtm.message_id = ml.message_id
         AND mtm.tag_id = t.id
        WHERE mtm.id IS NULL
    )"));
    if (q2.exec()) insertedMaps = q2.numRowsAffected();

    return qMakePair(insertedTags, insertedMaps);
}

void logTagProjectionInvariants(QSqlDatabase &database)
{
    QSqlQuery q1(database);
    q1.prepare(QStringLiteral(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE NOT EXISTS (
            SELECT 1
            FROM tags t
            WHERE t.normalized_name = lower(ml.label)
        )
    )"));
    int labelsMissingTag = -1;
    if (q1.exec() && q1.next()) labelsMissingTag = q1.value(0).toInt();

    QSqlQuery q2(database);
    q2.prepare(QStringLiteral(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE NOT EXISTS (
            SELECT 1
            FROM tags t
            JOIN message_tag_map mtm ON mtm.tag_id = t.id
            WHERE t.normalized_name = lower(ml.label)
              AND mtm.account_email = ml.account_email
              AND mtm.message_id = ml.message_id
        )
    )"));
    int labelsMissingTagMap = -1;
    if (q2.exec() && q2.next()) labelsMissingTagMap = q2.value(0).toInt();

}

QPair<int,int> repairLabelEdgeInvariants(QSqlDatabase &database)
{
    int insertedEdges = 0;
    int insertedLabels = 0;

    QSqlQuery q1(database);
    q1.prepare(QStringLiteral(R"(
        INSERT INTO message_folder_map (account_email, message_id, folder, uid, unread, source, confidence, observed_at)
        SELECT ml.account_email,
               ml.message_id,
               ml.label,
               COALESCE((
                   SELECT uid FROM message_folder_map m2
                   WHERE m2.account_email = ml.account_email AND m2.message_id = ml.message_id
                   ORDER BY m2.rowid DESC LIMIT 1
               ), ''),
               COALESCE((
                   SELECT unread FROM message_folder_map m3
                   WHERE m3.account_email = ml.account_email AND m3.message_id = ml.message_id
                   ORDER BY m3.rowid DESC LIMIT 1
               ), 1),
               'label-repair',
               95,
               datetime('now')
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )"));
    if (q1.exec()) insertedEdges = q1.numRowsAffected();

    QSqlQuery q2(database);
    q2.prepare(QStringLiteral(R"(
        INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
        SELECT mfm.account_email, mfm.message_id, mfm.folder, 'edge-repair', 95, datetime('now')
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
        ON CONFLICT(account_email, message_id, label) DO NOTHING
    )"));
    if (q2.exec()) insertedLabels = q2.numRowsAffected();

    return qMakePair(insertedEdges, insertedLabels);
}

void logLabelEdgeInvariants(QSqlDatabase &database)
{
    QSqlQuery q1(database);
    q1.prepare(QStringLiteral(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )"));
    int labelsMissingEdge = -1;
    if (q1.exec() && q1.next()) labelsMissingEdge = q1.value(0).toInt();

    QSqlQuery q2(database);
    q2.prepare(QStringLiteral(R"(
        SELECT count(*)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
    )"));
    int edgesMissingLabel = -1;
    if (q2.exec() && q2.next()) edgesMissingLabel = q2.value(0).toInt();

}

QString logicalMessageKey(const QString &accountEmail,
                         const QString &sender,
                         const QString &subject,
                         const QString &receivedAt)
{
    const QString normalized = accountEmail.trimmed().toLower() + QStringLiteral("\x1f")
            + sender.trimmed().toLower() + QStringLiteral("\x1f")
            + subject.trimmed().toLower() + QStringLiteral("\x1f")
            + receivedAt.trimmed();
    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool isTrashFolderName(const QString &folder)
{
    const QString f = folder.trimmed().toLower();
    if (f.isEmpty()) return false;
    return f == QStringLiteral("trash")
            || f == QStringLiteral("[gmail]/trash")
            || f == QStringLiteral("[google mail]/trash")
            || f.endsWith(QStringLiteral("/trash"));
}

bool isCategoryFolderName(const QString &folder)
{
    return folder.trimmed().toLower().contains(QStringLiteral("/categories/"));
}

bool isSystemLabelName(const QString &label)
{
    const QString l = label.trimmed().toLower();
    if (l.isEmpty()) return true;
    return l == QStringLiteral("inbox")
            || l == QStringLiteral("sent")
            || l == QStringLiteral("sent mail")
            || l == QStringLiteral("draft")
            || l == QStringLiteral("drafts")
            || l == QStringLiteral("trash")
            || l == QStringLiteral("spam")
            || l == QStringLiteral("junk")
            || l == QStringLiteral("all mail")
            || l == QStringLiteral("starred")
            || l == QStringLiteral("[gmail]")
            || l == QStringLiteral("[google mail]")
            || l.startsWith(QStringLiteral("[gmail]/"))
            || l.startsWith(QStringLiteral("[google mail]/"))
            || l.startsWith(QStringLiteral("\\"));
}

int folderEdgeCount(QSqlDatabase &database, const QString &accountEmail, const QString &folder)
{
    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT COUNT(*)
        FROM message_folder_map
        WHERE account_email=:account_email AND lower(folder)=:folder
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed().toLower());
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

int folderOverlapCount(QSqlDatabase &database,
                       const QString &accountEmail,
                       const QString &folder,
                       const QStringList &uids)
{
    if (uids.isEmpty()) return 0;

    QStringList placeholders;
    placeholders.reserve(uids.size());
    for (int i = 0; i < uids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT COUNT(*)
        FROM message_folder_map
        WHERE account_email=:account_email
          AND lower(folder)=:folder
          AND uid IN (%1)
    )").arg(placeholders.join(QStringLiteral(","))));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed().toLower());
    for (int i = 0; i < uids.size(); ++i) {
        q.bindValue(QStringLiteral(":u%1").arg(i), uids.at(i));
    }
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

int allMailOverlapCount(QSqlDatabase &database,
                        const QString &accountEmail,
                        const QStringList &uids)
{
    if (uids.isEmpty()) return 0;

    QStringList placeholders;
    placeholders.reserve(uids.size());
    for (int i = 0; i < uids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT COUNT(*)
        FROM message_folder_map
        WHERE account_email=:account_email
          AND (
            lower(folder)='[gmail]/all mail'
            OR lower(folder)='[google mail]/all mail'
            OR lower(folder) LIKE '%/all mail'
          )
          AND uid IN (%1)
    )").arg(placeholders.join(QStringLiteral(","))));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    for (int i = 0; i < uids.size(); ++i) {
        q.bindValue(QStringLiteral(":u%1").arg(i), uids.at(i));
    }
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

void upsertFolderEdge(QSqlDatabase &database,
                      const QString &accountEmail,
                      int messageId,
                      const QString &folder,
                      const QString &uid,
                      const QVariant &unread)
{
    QSqlQuery qMapAuth(database);
    qMapAuth.prepare(QStringLiteral(R"(
        INSERT INTO message_folder_map (account_email, message_id, folder, uid, unread, source, confidence, observed_at)
        VALUES (:account_email, :message_id, :folder, :uid, :unread, 'imap-label', 100, datetime('now'))
        ON CONFLICT(account_email, folder, uid) DO UPDATE SET
          message_id=excluded.message_id,
          unread=MIN(message_folder_map.unread, excluded.unread),
          source='imap-label',
          confidence=MAX(message_folder_map.confidence, 100),
          observed_at=datetime('now')
    )"));
    qMapAuth.bindValue(QStringLiteral(":account_email"), accountEmail);
    qMapAuth.bindValue(QStringLiteral(":message_id"), messageId);
    qMapAuth.bindValue(QStringLiteral(":folder"), folder);
    qMapAuth.bindValue(QStringLiteral(":uid"), uid);
    qMapAuth.bindValue(QStringLiteral(":unread"), unread);
    qMapAuth.exec();

}

int deleteFolderEdge(QSqlDatabase &database,
                     const QString &accountEmail,
                     const QString &folder,
                     const QString &uid)
{
    QSqlQuery qMap(database);
    qMap.prepare(QStringLiteral(R"(
        DELETE FROM message_folder_map
        WHERE account_email=:account_email AND folder=:folder AND uid=:uid
    )"));
    qMap.bindValue(QStringLiteral(":account_email"), accountEmail);
    qMap.bindValue(QStringLiteral(":folder"), folder);
    qMap.bindValue(QStringLiteral(":uid"), uid);
    qMap.exec();
    const int removed = qMap.numRowsAffected();

    return removed;
}

int pruneFolderEdgesToUids(QSqlDatabase &database,
                           const QString &accountEmail,
                           const QString &folder,
                           const QStringList &uids)
{
    QSqlQuery q(database);
    int removed = 0;
    if (uids.isEmpty()) {
        q.prepare(QStringLiteral("DELETE FROM message_folder_map WHERE account_email=:account_email AND folder=:folder"));
        q.bindValue(QStringLiteral(":account_email"), accountEmail);
        q.bindValue(QStringLiteral(":folder"), folder);
        q.exec();
        removed = q.numRowsAffected();
    } else {
        QStringList placeholders;
        placeholders.reserve(uids.size());
        for (int i = 0; i < uids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

        const QString sql = QStringLiteral(
                                "DELETE FROM message_folder_map WHERE account_email=:account_email AND folder=:folder AND uid NOT IN (%1)")
                                .arg(placeholders.join(QStringLiteral(",")));
        q.prepare(sql);
        q.bindValue(QStringLiteral(":account_email"), accountEmail);
        q.bindValue(QStringLiteral(":folder"), folder);
        for (int i = 0; i < uids.size(); ++i) {
            q.bindValue(QStringLiteral(":u%1").arg(i), uids.at(i));
        }
        q.exec();
        removed = q.numRowsAffected();
    }

    return removed;
}

}

DataStore::DataStore(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("kestrel_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

DataStore::~DataStore()
{
    const auto conn = m_connectionName;
    {
        auto d = QSqlDatabase::database(conn, false);
        if (d.isValid()) {
            d.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
}

QSqlDatabase DataStore::db() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool DataStore::init()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/kestrel-mail");
    QDir().mkpath(base);
    const QString path = base + QStringLiteral("/mail.db");

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(path);
    if (!database.open()) {
        return false;
    }

    QSqlQuery q(database);

    // Finalized schema cleanup:
    // - drop legacy pre-refactor messages table
    // - rename canonical_messages -> messages
    if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='canonical_messages'"))
            && q.next()) {
        q.exec(QStringLiteral("DROP TABLE IF EXISTS messages"));
        q.exec(QStringLiteral("ALTER TABLE canonical_messages RENAME TO messages"));
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS messages (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          logical_key TEXT NOT NULL,
          sender TEXT,
          recipient TEXT,
          subject TEXT,
          received_at TEXT,
          snippet TEXT,
          body_html TEXT,
          avatar_domain TEXT,
          avatar_url TEXT,
          avatar_source TEXT,
          unread INTEGER DEFAULT 1,
          has_tracking_pixel INTEGER DEFAULT 0,
          UNIQUE(account_email, logical_key)
        )
    )"))) {
        return false;
    }

    // Forward-compatible migration for existing DBs.
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN avatar_domain TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN avatar_url TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN avatar_source TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN message_id_header TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN gm_msg_id TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN recipient TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN recipient_avatar_url TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN list_unsubscribe TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN reply_to TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN return_path TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN auth_results TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN x_mailer TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN in_reply_to TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN references_header TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN esp_vendor TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN thread_id TEXT"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_thread ON messages(account_email, thread_id)"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN gm_thr_id TEXT"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_gm_thr ON messages(account_email, gm_thr_id)"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN has_tracking_pixel INTEGER DEFAULT 0"));

    // Backfill thread_id for existing messages (new ones get it in upsertHeader).
    {
        QSqlQuery bfQ(database);
        bfQ.exec(QStringLiteral(
            "SELECT id, message_id_header, in_reply_to, references_header "
            "FROM messages WHERE thread_id IS NULL OR length(trim(thread_id)) = 0 LIMIT 10000"
        ));
        QSqlQuery upQ(database);
        upQ.prepare(QStringLiteral("UPDATE messages SET thread_id=:tid WHERE id=:id"));
        while (bfQ.next()) {
            const QString tid = computeThreadId(
                bfQ.value(3).toString(), bfQ.value(2).toString(), bfQ.value(1).toString());
            if (!tid.isEmpty()) {
                upQ.bindValue(QStringLiteral(":tid"), tid);
                upQ.bindValue(QStringLiteral(":id"), bfQ.value(0).toInt());
                upQ.exec();
            }
        }
    }

    // Clear raw HTTPS Google profile photo URLs from contact_avatars — they require auth.
    q.exec(QStringLiteral(
        "UPDATE contact_avatars SET avatar_url='', failure_count=0, last_checked_at='2000-01-01T00:00:00' "
        "WHERE source='google-people' "
        "AND (avatar_url LIKE 'https://%' OR avatar_url LIKE 'http://%')"
    ));

    // Clear any raw s2/favicons URLs that slipped through (redirect to faviconV2 which 404s).
    q.exec(QStringLiteral(
        "UPDATE contact_avatars SET avatar_url='', failure_count=0, last_checked_at='2000-01-01T00:00:00' "
        "WHERE avatar_url LIKE 'https://www.google.com/s2/favicons%'"
    ));

    // Migrate existing data URI avatar entries to files on disk.
    // After this migration all avatar_url values are either file:// URLs or empty.
    {
        QSqlQuery qSel(database);
        if (qSel.exec(QStringLiteral("SELECT email, avatar_url FROM contact_avatars WHERE avatar_url LIKE 'data:%'"))) {
            struct MigrateRow { QString email; QString url; };
            QVector<MigrateRow> rows;
            while (qSel.next())
                rows.push_back({qSel.value(0).toString(), qSel.value(1).toString()});
            for (const auto &row : rows) {
                const QString fileUrl = writeAvatarDataUri(row.email, row.url);
                QSqlQuery qUp(database);
                qUp.prepare(QStringLiteral(
                    "UPDATE contact_avatars SET avatar_url=:url WHERE email=:email"));
                qUp.bindValue(QStringLiteral(":url"), fileUrl);  // empty on failure → clears blob
                qUp.bindValue(QStringLiteral(":email"), row.email);
                qUp.exec();
            }
        }
    }

    // Clear stale esp_vendor values produced by the old Received-chain heuristic.
    // Only values from definitive X-* header markers are kept; everything else is
    // garbage (sender's own infrastructure, ugly subdomains like "Zillowmail", etc.).
    q.exec(QStringLiteral(
        "UPDATE messages SET esp_vendor = NULL "
        "WHERE esp_vendor IS NOT NULL "
        "AND esp_vendor NOT IN ('Mailgun','Sendgrid','Mailchimp','Klaviyo','Postmark','Amazon SES')"
    ));
    // Clear snippets that contain raw HTML/DOCTYPE content — produced by a bug where
    // extractRfc822Literal returned the BODY[HEADER.FIELDS] literal instead of BODY[],
    // or where HTML-only emails fed raw HTML into the plain-text path.
    // Nulling these out causes them to be re-fetched by the fixed snippet pipeline.
    q.exec(QStringLiteral(
        "UPDATE messages SET snippet = NULL "
        "WHERE snippet IS NOT NULL AND ("
        "  snippet LIKE '<!DOCTYPE%' OR snippet LIKE '<%html%' OR "
        "  snippet LIKE '<img%' OR snippet LIKE '<[%' OR "
        "  snippet LIKE '<table%' OR snippet LIKE '<div%' OR "
        "  snippet LIKE '<style%' OR snippet LIKE '<script%'"
        ")"
    ));

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS message_folder_map (
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          folder TEXT NOT NULL,
          uid TEXT NOT NULL,
          unread INTEGER NOT NULL DEFAULT 1,
          source TEXT NOT NULL DEFAULT 'imap-label',
          confidence INTEGER NOT NULL DEFAULT 100,
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          PRIMARY KEY(account_email, folder, uid),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"))) {
        return false;
    }
    q.exec(QStringLiteral("ALTER TABLE message_folder_map ADD COLUMN unread INTEGER NOT NULL DEFAULT 1"));

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS contact_avatars (
          email TEXT PRIMARY KEY,
          avatar_url TEXT,
          source TEXT,
          last_checked_at TEXT NOT NULL DEFAULT (datetime('now')),
          etag TEXT,
          failure_count INTEGER NOT NULL DEFAULT 0
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS contact_display_names (
          email TEXT PRIMARY KEY,
          display_name TEXT NOT NULL,
          source TEXT,
          display_score INTEGER NOT NULL DEFAULT 0,
          last_seen_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"))) {
        return false;
    }
    q.exec(QStringLiteral("ALTER TABLE contact_display_names ADD COLUMN display_score INTEGER NOT NULL DEFAULT 0"));

    // Migration scaffold: explicit label/provenance store (eM-style direction).
    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS message_labels (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          label TEXT NOT NULL,
          source TEXT NOT NULL DEFAULT 'imap-label',
          confidence INTEGER NOT NULL DEFAULT 100,
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          UNIQUE(account_email, message_id, label),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"))) {
        return false;
    }

    // eM-inspired normalization: keep per-message address rows to avoid global
    // display-name poisoning and to preserve message-local participant evidence.
    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS message_participants (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          role TEXT NOT NULL,
          position INTEGER NOT NULL DEFAULT 0,
          display_name TEXT,
          address TEXT,
          source TEXT NOT NULL DEFAULT 'header',
          UNIQUE(account_email, message_id, role, position),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"))) {
        return false;
    }

    // Unified global tags (client + server-origin labels) inspired by mature client schemas.
    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS tags (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          name TEXT NOT NULL,
          normalized_name TEXT NOT NULL UNIQUE,
          color TEXT,
          origin TEXT NOT NULL DEFAULT 'server',
          created_at TEXT NOT NULL DEFAULT (datetime('now')),
          updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS message_tag_map (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          message_id INTEGER NOT NULL,
          tag_id INTEGER NOT NULL,
          source TEXT NOT NULL DEFAULT 'server',
          observed_at TEXT NOT NULL DEFAULT (datetime('now')),
          UNIQUE(account_email, message_id, tag_id),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE,
          FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE
        )
    )"))) {
        return false;
    }


    // Backfill unified tags from observed labels.
    q.exec(QStringLiteral(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        SELECT ml.label, lower(ml.label), 'server', datetime('now')
        FROM message_labels ml
        GROUP BY lower(ml.label)
        ON CONFLICT(normalized_name) DO UPDATE SET
          updated_at=datetime('now')
    )"));

    q.exec(QStringLiteral(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT ml.account_email, ml.message_id, t.id, 'server', datetime('now')
        FROM message_labels ml
        JOIN tags t ON t.normalized_name = lower(ml.label)
        ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
          observed_at=datetime('now')
    )"));


    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS folders (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          name TEXT NOT NULL,
          flags TEXT,
          special_use TEXT,
          UNIQUE(account_email, name)
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS folder_sync_status (
          account_email TEXT NOT NULL,
          folder TEXT NOT NULL,
          uid_next INTEGER,
          highest_modseq INTEGER,
          messages INTEGER,
          updated_at TEXT NOT NULL DEFAULT (datetime('now')),
          PRIMARY KEY(account_email, folder)
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS sender_image_permissions (
          domain TEXT PRIMARY KEY,
          granted_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS message_attachments (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          message_id INTEGER NOT NULL,
          account_email TEXT NOT NULL,
          part_id TEXT NOT NULL,
          name TEXT NOT NULL DEFAULT '',
          mime_type TEXT NOT NULL DEFAULT '',
          encoded_bytes INTEGER NOT NULL DEFAULT 0,
          encoding TEXT NOT NULL DEFAULT '',
          UNIQUE(account_email, message_id, part_id),
          FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
        )
    )"))) {
        return false;
    }

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS favorites_config (
          key     TEXT PRIMARY KEY,
          enabled INTEGER NOT NULL DEFAULT 1
        )
    )"))) {
        return false;
    }

    // Seed defaults: All Inboxes / Unread / Flagged visible; rest hidden.
    q.exec(QStringLiteral(R"(
        INSERT OR IGNORE INTO favorites_config (key, enabled) VALUES
          ('all-inboxes', 1),
          ('unread',      1),
          ('flagged',     1),
          ('outbox',      0),
          ('sent',        0),
          ('trash',       0),
          ('drafts',      0),
          ('junk',        0),
          ('archive',     0),
          ('unreplied',   0),
          ('snoozed',     0)
    )"));

    if (!q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS user_folders (
          id         INTEGER PRIMARY KEY AUTOINCREMENT,
          name       TEXT    NOT NULL UNIQUE,
          sort_order INTEGER NOT NULL DEFAULT 0,
          created_at TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )"))) {
        return false;
    }

    // Paging/list performance indexes.
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_received_at_id ON messages(received_at DESC, id DESC)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_folder_message ON message_folder_map(folder, message_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_account_message ON message_folder_map(account_email, message_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_account_folder_uid ON message_folder_map(account_email, folder, uid)"));
    // Expression index so lower(folder) comparisons in fetchCandidatesForMessageKey use the index
    // instead of a full table scan (lower() on a plain-column index is not usable by SQLite).
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_account_lf_uid ON message_folder_map(account_email, lower(folder), uid)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ml_account_message_label ON message_labels(account_email, message_id, label)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ml_label_lower ON message_labels(lower(label))"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mtm_account_message_tag ON message_tag_map(account_email, message_id, tag_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mp_address_lower ON message_participants(lower(address))"));

    // Cleanup: remove poisoned display names that look like address lists/emails.
    q.exec(QStringLiteral(R"(
        DELETE FROM contact_display_names
        WHERE instr(display_name, '@') > 0
           OR instr(display_name, ',') > 0
           OR instr(display_name, ';') > 0
    )"));

    // Migration cleanup: remove category folder edges (categories are labels/tags only).
    const int purgedCategoryEdges = purgeCategoryFolderEdges(database);
    if (purgedCategoryEdges > 0) {
        qInfo().noquote() << "[migration-cleanup]" << "purgedCategoryEdges=" << purgedCategoryEdges;
    }

    // Backfill missing/empty contact display names from participant evidence.
    q.exec(QStringLiteral(R"(
        WITH ranked AS (
            SELECT lower(address) AS email,
                   trim(display_name) AS display_name,
                   count(*) AS n,
                   row_number() OVER (
                     PARTITION BY lower(address)
                     ORDER BY count(*) DESC, length(trim(display_name)) DESC, trim(display_name) ASC
                   ) AS rn
            FROM message_participants
            WHERE address IS NOT NULL
              AND trim(address) <> ''
              AND display_name IS NOT NULL
              AND trim(display_name) <> ''
              AND instr(display_name, '@') = 0
              AND instr(display_name, ',') = 0
              AND instr(display_name, ';') = 0
            GROUP BY lower(address), trim(display_name)
        )
        INSERT INTO contact_display_names (email, display_name, source, display_score, last_seen_at)
        SELECT r.email, r.display_name, 'participant-backfill', 500, datetime('now')
        FROM ranked r
        LEFT JOIN contact_display_names c ON c.email = r.email
        WHERE r.rn = 1
          AND (c.email IS NULL OR length(trim(coalesce(c.display_name,''))) = 0)
        ON CONFLICT(email) DO UPDATE SET
          display_name = excluded.display_name,
          display_score = excluded.display_score,
          source = excluded.source,
          last_seen_at = datetime('now')
    )"));

    // Repair pass: if a weak/empty canonical message row was created for a uid that
    // already maps to a stronger identified message, re-point the edge and let orphan
    // cleanup remove the weak row.
    q.exec(QStringLiteral(R"(
        UPDATE message_folder_map AS m
        SET message_id = (
            SELECT m2.message_id
            FROM message_folder_map m2
            JOIN messages s ON s.id = m2.message_id
            WHERE m2.account_email = m.account_email
              AND m2.uid = m.uid
              AND m2.message_id <> m.message_id
              AND (
                length(trim(COALESCE(s.sender, ''))) > 0
                OR length(trim(COALESCE(s.subject, ''))) > 0
                OR length(trim(COALESCE(s.message_id_header, ''))) > 0
                OR length(trim(COALESCE(s.gm_msg_id, ''))) > 0
              )
            ORDER BY m2.rowid DESC
            LIMIT 1
        )
        WHERE EXISTS (
            SELECT 1 FROM messages w
            WHERE w.id = m.message_id
              AND length(trim(COALESCE(w.sender, ''))) = 0
              AND length(trim(COALESCE(w.subject, ''))) = 0
              AND length(trim(COALESCE(w.message_id_header, ''))) = 0
              AND length(trim(COALESCE(w.gm_msg_id, ''))) = 0
        )
    )"));

    reloadInbox();
    reloadFolders();
    return true;
}

void DataStore::upsertHeaders(const QVariantList &headers)
{
    if (headers.isEmpty()) return;

    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) return;
        database = db();
    }

    database.transaction();
    for (const QVariant &v : headers) {
        if (const QVariantMap h = v.toMap(); !h.isEmpty())
            upsertHeader(h);
    }
    database.commit();

    static int invariantTick = 0;
    ++invariantTick;
    if (invariantTick % 20 == 0) {
        logLabelEdgeInvariants(database);
        logTagProjectionInvariants(database);

        static const bool repairLabelEdgeEnabled = []() {
            bool ok = false;
            const int v = qEnvironmentVariableIntValue("KESTREL_INVARIANT_LABEL_EDGE_REPAIR", &ok);
            return ok && v == 1;
        }();
        static const bool repairTagProjectionEnabled = []() {
            bool ok = false;
            const int v = qEnvironmentVariableIntValue("KESTREL_INVARIANT_TAG_PROJECTION_REPAIR", &ok);
            return ok && v == 1;
        }();

        if (repairLabelEdgeEnabled)
            (void)repairLabelEdgeInvariants(database);

        if (repairTagProjectionEnabled)
            (void)repairTagProjectionInvariants(database);
    }
}

void DataStore::upsertHeader(const QVariantMap &header)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) return;
        database = db();
    }

    auto sanitizeSnippet = [](const QString &snippetRaw, const QString &subjectRaw) {
        auto normalizeSnippetWhitespace = [](QString s) {
            s.replace('\t', ' ');
            s.replace('\r', ' ');
            s.replace('\n', ' ');
            s.remove(QChar(0x200B));
            s.remove(QChar(0x200C));
            s.remove(QChar(0x200D));
            s.remove(QChar(0x2060));
            s.remove(QChar(0xFEFF));
            s.remove(QChar(0x00AD));
            s.remove(QChar(0xFFFC));

            QString filtered;
            filtered.reserve(s.size());
            for (const QChar ch : s) {
                const auto cat = ch.category();
                if (cat == QChar::Other_Control || cat == QChar::Other_Format || cat == QChar::Other_NotAssigned
                        || cat == QChar::Other_PrivateUse || cat == QChar::Other_Surrogate) {
                    continue;
                }
                // Keep all Unicode letters/numbers/marks/punctuation/symbols (incl. Japanese),
                // drop only problematic control/format codepoints.
                filtered.append(ch);
            }
            s = filtered.trimmed();

            QString out;
            out.reserve(s.size());
            int spaceRun = 0;
            for (const QChar ch : s) {
                if (ch.isSpace()) {
                    ++spaceRun;
                    if (spaceRun <= 1) out.append(' ');
                } else {
                    spaceRun = 0;
                    out.append(ch);
                }
            }
            return out.trimmed();
        };

        QString s = normalizeSnippetWhitespace(snippetRaw);
        const QString originalNormalized = s;
        const bool hadUrl = QRegularExpression(QStringLiteral("https?://\\S+"), QRegularExpression::CaseInsensitiveOption).match(s).hasMatch();
        s.replace(QRegularExpression(QStringLiteral("https?://\\S+"), QRegularExpression::CaseInsensitiveOption), QString());
        s.replace(QRegularExpression(QStringLiteral("\\b(?:charset|boundary)\\s*=\\s*\"?[^\"\\s]+\"?"), QRegularExpression::CaseInsensitiveOption), QString());
        s = normalizeSnippetWhitespace(s);

        // Strip common web-view boilerplate (at start or embedded), including optional URLs.
        s.replace(QRegularExpression(QStringLiteral("(?i)view\\s+(?:this\\s+)?email\\s+in\\s+(?:a|your)?\\s*browser[:!\\-\\s]*(?:https?://\\S+)?")), QString());
        s.replace(QRegularExpression(QStringLiteral("(?i)view\\s+in\\s+(?:a|your)?\\s*browser[:!\\-\\s]*(?:https?://\\S+)?")), QString());
        s.replace(QRegularExpression(QStringLiteral("(?i)view\\s+as\\s+(?:a\\s+)?web\\s+page[:!\\-\\s]*(?:https?://\\S+)?")), QString());
        s = normalizeSnippetWhitespace(s);

        s.replace(QRegularExpression(QStringLiteral("\\s*[()\\[\\]{}|:;.,-]+\\s*$")), QString());
        s = normalizeSnippetWhitespace(s);

        const QString t = s.toLower();
        const int alphaCount = s.count(QRegularExpression(QStringLiteral("[A-Za-z]")));
        const bool danglingShort = s.endsWith('(') || s.endsWith(':') || s.endsWith('-');
        const bool junk = s.isEmpty()
                || t.startsWith(QStringLiteral("* "))
                || t.contains(QStringLiteral(" fetch ("))
                || t.contains(QStringLiteral("body[header.fields"))
                || t.contains(QStringLiteral("x-gm-labels"))
                || t.contains(QStringLiteral("body[text]"))
                || t.contains(QStringLiteral("ok success"))
                || t.contains(QStringLiteral("throttled"))
                || t.contains(QStringLiteral("this is a multi-part message in mime format"))
                || t.contains(QStringLiteral("view this email in your browser"))
                || t.contains(QStringLiteral("view as a web page"))
                || t.contains(QStringLiteral("it looks like your email client might not support html"))
                || t.contains(QStringLiteral("try opening this email in another email client"))
                || (hadUrl && (alphaCount < 20 || danglingShort));
        if (junk) {
            const QString subject = normalizeSnippetWhitespace(subjectRaw);
            if (!subject.isEmpty()) return subject.left(140);
            if (!originalNormalized.isEmpty()) return originalNormalized.left(140);
            return QString();
        }
        return s.left(140);
    };

    const QString accountEmail = header.value(QStringLiteral("accountEmail")).toString();
    const QString folderValue = header.value(QStringLiteral("folder"), QStringLiteral("INBOX")).toString();
    const QString uidValue = header.value(QStringLiteral("uid")).toString();
    const QString subjectValue = header.value(QStringLiteral("subject")).toString();
    const QString rawSnippetValue = header.value(QStringLiteral("snippet")).toString();
    const QString senderValue = header.value(QStringLiteral("sender")).toString();
    const QString recipientValue = header.value(QStringLiteral("recipient")).toString();
    const QString recipientAvatarUrlValue = header.value(QStringLiteral("recipientAvatarUrl")).toString().trimmed();
    const bool recipientAvatarLookupMiss = header.value(QStringLiteral("recipientAvatarLookupMiss"), false).toBool();
    const QString senderEmailValue = extractFirstEmail(senderValue);
    const QString recipientEmailValue = extractFirstEmail(recipientValue);
    const QString senderDisplayNameValue = extractExplicitDisplayName(senderValue, senderEmailValue);
    const QString recipientDisplayNameValue = extractExplicitDisplayName(recipientValue, recipientEmailValue);
    const QString receivedAtValue = header.value(QStringLiteral("receivedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).toString();
    const QString snippetValue = sanitizeSnippet(rawSnippetValue, subjectValue);
    const QString bodyHtmlValue = header.value(QStringLiteral("bodyHtml")).toString().trimmed();
    const QString avatarUrlValue = header.value(QStringLiteral("avatarUrl")).toString().trimmed();
    const QString avatarSourceValue = header.value(QStringLiteral("avatarSource")).toString().trimmed().toLower();
    const int unreadValue = header.value(QStringLiteral("unread"), true).toBool() ? 1 : 0;
    const QString messageIdHeaderValue = header.value(QStringLiteral("messageIdHeader")).toString().trimmed();
    const QString gmMsgIdValue = header.value(QStringLiteral("gmMsgId")).toString().trimmed();
    const QString gmThrIdValue = header.value(QStringLiteral("gmThrId")).toString().trimmed();
    const QString listUnsubscribeValue  = header.value(QStringLiteral("listUnsubscribe")).toString().trimmed();
    const QString replyToValue          = header.value(QStringLiteral("replyTo")).toString().trimmed();
    const QString returnPathValue       = header.value(QStringLiteral("returnPath")).toString().trimmed();
    const QString authResultsValue      = header.value(QStringLiteral("authResults")).toString().trimmed();
    const QString xMailerValue          = header.value(QStringLiteral("xMailer")).toString().trimmed();
    const QString inReplyToValue        = header.value(QStringLiteral("inReplyTo")).toString().trimmed();
    const QString referencesValue       = header.value(QStringLiteral("references")).toString().trimmed();
    const QString espVendorValue        = header.value(QStringLiteral("espVendor")).toString().trimmed();
    const bool primaryLabelObserved = header.value(QStringLiteral("primaryLabelObserved"), false).toBool();
    const QString rawGmailLabels = header.value(QStringLiteral("rawGmailLabels")).toString();

    // Guardrail: avoid creating synthetic/empty canonical rows when we only have
    // a folder+uid edge (common during on-demand hydration/category relabel paths).
    const bool weakIdentity = senderValue.trimmed().isEmpty()
            && subjectValue.trimmed().isEmpty()
            && messageIdHeaderValue.isEmpty()
            && gmMsgIdValue.isEmpty();
    if (weakIdentity && !uidValue.trimmed().isEmpty()) {
        QSqlQuery qExisting(database);
        qExisting.prepare(QStringLiteral(R"(
            SELECT m.id, m.sender
            FROM message_folder_map mfm
            JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email
            WHERE mfm.account_email=:account_email
              AND lower(mfm.folder)=lower(:folder)
              AND mfm.uid=:uid
              AND (
                length(trim(COALESCE(m.sender, ''))) > 0
                OR length(trim(COALESCE(m.subject, ''))) > 0
                OR length(trim(COALESCE(m.message_id_header, ''))) > 0
                OR length(trim(COALESCE(m.gm_msg_id, ''))) > 0
              )
            ORDER BY mfm.rowid DESC
            LIMIT 1
        )"));
        qExisting.bindValue(QStringLiteral(":account_email"), accountEmail);
        qExisting.bindValue(QStringLiteral(":folder"), folderValue);
        qExisting.bindValue(QStringLiteral(":uid"), uidValue);
        if (qExisting.exec() && qExisting.next()) {
            const int existingMessageId = qExisting.value(0).toInt();
            const QString existingSender = qExisting.value(1).toString();
            qInfo().noquote() << "[upsert-weak-reuse]"
                              << "account=" << accountEmail
                              << "folder=" << folderValue
                              << "uid=" << uidValue
                              << "messageId=" << existingMessageId
                              << "hasBody=" << (!bodyHtmlValue.trimmed().isEmpty());

            // Hydration path often arrives as uid-only with body_html; keep body update.
            if (!bodyHtmlValue.trimmed().isEmpty()) {
                const int hasTP = computeHasTrackingPixel(bodyHtmlValue, existingSender) ? 1 : 0;
                QSqlQuery qBody(database);
                qBody.prepare(QStringLiteral(R"(
                    UPDATE messages
                    SET body_html = CASE
                                      WHEN :body_html IS NOT NULL
                                           AND length(trim(:body_html)) > length(trim(COALESCE(body_html, '')))
                                      THEN :body_html
                                      ELSE body_html
                                    END,
                        has_tracking_pixel = :has_tp,
                        unread = :unread
                    WHERE id=:message_id AND account_email=:account_email
                )"));
                qBody.bindValue(QStringLiteral(":body_html"), bodyHtmlValue);
                qBody.bindValue(QStringLiteral(":has_tp"), hasTP);
                qBody.bindValue(QStringLiteral(":unread"), unreadValue);
                qBody.bindValue(QStringLiteral(":message_id"), existingMessageId);
                qBody.bindValue(QStringLiteral(":account_email"), accountEmail);
                qBody.exec();
            }

            upsertFolderEdge(database, accountEmail, existingMessageId, folderValue, uidValue, unreadValue);
            scheduleReloadInbox();
            return;
        }
    }

    if (weakIdentity) {
        qWarning().noquote() << "[upsert-weak-new]"
                             << "account=" << accountEmail
                             << "folder=" << folderValue
                             << "uid=" << uidValue
                             << "reason=no-existing-strong-row";
    }

    const QString lkey = logicalMessageKey(accountEmail, senderValue, subjectValue, receivedAtValue);

    const QString subjectNormForLog = subjectValue.trimmed();
    const QString rawNormForLog = rawSnippetValue.trimmed();
    const QString sanNormForLog = snippetValue.trimmed();
    if (subjectNormForLog.compare(QStringLiteral("T-Minus 26 Days Until Spring"), Qt::CaseInsensitive) == 0
            || (sanNormForLog.compare(subjectNormForLog, Qt::CaseInsensitive) == 0 && !rawNormForLog.isEmpty()
                && rawNormForLog.compare(subjectNormForLog, Qt::CaseInsensitive) != 0)) {
    }

    QSqlQuery qCanon(database);
    qCanon.prepare(QStringLiteral(R"(
        INSERT INTO messages (account_email, logical_key, sender, recipient, subject, received_at, snippet, body_html, message_id_header, gm_msg_id, unread, list_unsubscribe, reply_to, return_path, auth_results, x_mailer, in_reply_to, references_header, esp_vendor, thread_id, gm_thr_id, has_tracking_pixel)
        VALUES (:account_email, :logical_key, :sender, :recipient, :subject, :received_at, :snippet, :body_html, :message_id_header, :gm_msg_id, :unread, :list_unsubscribe, :reply_to, :return_path, :auth_results, :x_mailer, :in_reply_to, :references_header, :esp_vendor, :thread_id, :gm_thr_id, :has_tracking_pixel)
        ON CONFLICT(account_email, logical_key) DO UPDATE SET
          sender = excluded.sender,
          recipient = CASE
                        WHEN excluded.recipient IS NOT NULL
                             AND length(trim(excluded.recipient)) > 0
                        THEN excluded.recipient
                        ELSE messages.recipient
                      END,
          subject = excluded.subject,
          received_at = excluded.received_at,
          snippet = CASE
                      WHEN excluded.snippet IS NOT NULL
                      THEN excluded.snippet
                      ELSE messages.snippet
                    END,
          body_html = CASE
                        WHEN excluded.body_html IS NOT NULL
                             AND length(trim(excluded.body_html)) > length(trim(COALESCE(messages.body_html, '')))
                        THEN excluded.body_html
                        ELSE messages.body_html
                      END,
          message_id_header = CASE
                                WHEN excluded.message_id_header IS NOT NULL
                                     AND length(trim(excluded.message_id_header)) > 0
                                THEN excluded.message_id_header
                                ELSE messages.message_id_header
                              END,
          gm_msg_id = CASE
                        WHEN excluded.gm_msg_id IS NOT NULL
                             AND length(trim(excluded.gm_msg_id)) > 0
                        THEN excluded.gm_msg_id
                        ELSE messages.gm_msg_id
                      END,
          unread = excluded.unread,
          list_unsubscribe = CASE
                               WHEN excluded.list_unsubscribe IS NOT NULL
                                    AND length(trim(excluded.list_unsubscribe)) > 0
                               THEN excluded.list_unsubscribe
                               ELSE messages.list_unsubscribe
                             END,
          reply_to = CASE
                       WHEN excluded.reply_to IS NOT NULL AND length(trim(excluded.reply_to)) > 0
                       THEN excluded.reply_to ELSE messages.reply_to END,
          return_path = CASE
                          WHEN excluded.return_path IS NOT NULL AND length(trim(excluded.return_path)) > 0
                          THEN excluded.return_path ELSE messages.return_path END,
          auth_results = CASE
                           WHEN excluded.auth_results IS NOT NULL AND length(trim(excluded.auth_results)) > 0
                           THEN excluded.auth_results ELSE messages.auth_results END,
          x_mailer = CASE
                       WHEN excluded.x_mailer IS NOT NULL AND length(trim(excluded.x_mailer)) > 0
                       THEN excluded.x_mailer ELSE messages.x_mailer END,
          in_reply_to = CASE
                          WHEN excluded.in_reply_to IS NOT NULL AND length(trim(excluded.in_reply_to)) > 0
                          THEN excluded.in_reply_to ELSE messages.in_reply_to END,
          references_header = CASE
                                WHEN excluded.references_header IS NOT NULL AND length(trim(excluded.references_header)) > 0
                                THEN excluded.references_header ELSE messages.references_header END,
          esp_vendor = excluded.esp_vendor,
          thread_id = CASE
                        WHEN excluded.thread_id IS NOT NULL AND length(trim(excluded.thread_id)) > 0
                        THEN excluded.thread_id ELSE messages.thread_id END,
          gm_thr_id = CASE
                        WHEN excluded.gm_thr_id IS NOT NULL AND length(trim(excluded.gm_thr_id)) > 0
                        THEN excluded.gm_thr_id ELSE messages.gm_thr_id END,
          has_tracking_pixel = CASE
                                  WHEN excluded.body_html IS NOT NULL
                                       AND length(trim(excluded.body_html)) > 0
                                  THEN excluded.has_tracking_pixel
                                  ELSE messages.has_tracking_pixel
                                END
    )"));
    qCanon.bindValue(QStringLiteral(":account_email"), accountEmail);
    qCanon.bindValue(QStringLiteral(":logical_key"), lkey);
    qCanon.bindValue(QStringLiteral(":sender"), senderValue);
    qCanon.bindValue(QStringLiteral(":recipient"), recipientValue);
    qCanon.bindValue(QStringLiteral(":subject"), subjectValue);
    qCanon.bindValue(QStringLiteral(":received_at"), receivedAtValue);
    qCanon.bindValue(QStringLiteral(":snippet"), snippetValue);
    qCanon.bindValue(QStringLiteral(":body_html"), bodyHtmlValue);
    qCanon.bindValue(QStringLiteral(":message_id_header"), messageIdHeaderValue);
    qCanon.bindValue(QStringLiteral(":gm_msg_id"), gmMsgIdValue);
    qCanon.bindValue(QStringLiteral(":unread"), unreadValue);
    qCanon.bindValue(QStringLiteral(":list_unsubscribe"),    listUnsubscribeValue.isEmpty() ? QVariant() : QVariant(listUnsubscribeValue));
    qCanon.bindValue(QStringLiteral(":reply_to"),           replyToValue.isEmpty()        ? QVariant() : QVariant(replyToValue));
    qCanon.bindValue(QStringLiteral(":return_path"),        returnPathValue.isEmpty()     ? QVariant() : QVariant(returnPathValue));
    qCanon.bindValue(QStringLiteral(":auth_results"),       authResultsValue.isEmpty()    ? QVariant() : QVariant(authResultsValue));
    qCanon.bindValue(QStringLiteral(":x_mailer"),           xMailerValue.isEmpty()        ? QVariant() : QVariant(xMailerValue));
    qCanon.bindValue(QStringLiteral(":in_reply_to"),        inReplyToValue.isEmpty()      ? QVariant() : QVariant(inReplyToValue));
    qCanon.bindValue(QStringLiteral(":references_header"),  referencesValue.isEmpty()     ? QVariant() : QVariant(referencesValue));
    qCanon.bindValue(QStringLiteral(":esp_vendor"),         espVendorValue.isEmpty()      ? QVariant() : QVariant(espVendorValue));
    qCanon.bindValue(QStringLiteral(":gm_thr_id"),          gmThrIdValue.isEmpty()         ? QVariant() : QVariant(gmThrIdValue));
    qCanon.bindValue(QStringLiteral(":has_tracking_pixel"), computeHasTrackingPixel(bodyHtmlValue, senderValue) ? 1 : 0);
    {
        // For Gmail messages with X-GM-THRID, use it as the canonical thread ID.
        // This groups all messages in the same Gmail conversation correctly even
        // when References/In-Reply-To headers are missing or broken.
        QString threadIdValue;
        if (!gmThrIdValue.isEmpty())
            threadIdValue = QStringLiteral("gm:") + gmThrIdValue;
        else
            threadIdValue = computeThreadId(referencesValue, inReplyToValue, messageIdHeaderValue);
        qCanon.bindValue(QStringLiteral(":thread_id"), threadIdValue.isEmpty() ? QVariant() : QVariant(threadIdValue));
    }
    qCanon.exec();

    // When a Gmail thread ID is known, update ALL sibling messages in the same
    // conversation that haven't been assigned this thread_id yet. This fixes
    // existing messages that were synced before X-GM-THRID was added to the fetch.
    if (!gmThrIdValue.isEmpty()) {
        const QString newTid = QStringLiteral("gm:") + gmThrIdValue;
        QSqlQuery sibQ(database);
        sibQ.prepare(QStringLiteral(
            "UPDATE messages SET thread_id=:tid WHERE account_email=:acct "
            "AND gm_thr_id=:gm_thr_id AND (thread_id IS NULL OR thread_id != :tid)"));
        sibQ.bindValue(QStringLiteral(":tid"),      newTid);
        sibQ.bindValue(QStringLiteral(":acct"),     accountEmail);
        sibQ.bindValue(QStringLiteral(":gm_thr_id"), gmThrIdValue);
        sibQ.exec();
    }

    QSqlQuery idQ(database);
    idQ.prepare(QStringLiteral("SELECT id FROM messages WHERE account_email=:account_email AND logical_key=:logical_key LIMIT 1"));
    idQ.bindValue(QStringLiteral(":account_email"), accountEmail);
    idQ.bindValue(QStringLiteral(":logical_key"), lkey);
    if (!idQ.exec() || !idQ.next()) {
        return;
    }
    const int messageId = idQ.value(0).toInt();

    const bool isCategoryFolder = folderValue.contains(QStringLiteral("/Categories/"), Qt::CaseInsensitive);
    if (!isCategoryFolder) {
        upsertFolderEdge(database, accountEmail, messageId, folderValue, uidValue, unreadValue);
    } else {
        QSqlQuery qLabel(database);
        qLabel.prepare(QStringLiteral(R"(
            INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
            VALUES (:account_email, :message_id, :label, 'category-folder-sync', 100, datetime('now'))
            ON CONFLICT(account_email, message_id, label) DO UPDATE SET
              source='category-folder-sync',
              confidence=MAX(message_labels.confidence, 100),
              observed_at=datetime('now')
        )"));
        qLabel.bindValue(QStringLiteral(":account_email"), accountEmail);
        qLabel.bindValue(QStringLiteral(":message_id"), messageId);
        qLabel.bindValue(QStringLiteral(":label"), folderValue);
        qLabel.exec();

        QSqlQuery qTag(database);
        qTag.prepare(QStringLiteral(R"(
            INSERT INTO tags (name, normalized_name, origin, updated_at)
            VALUES (:name, lower(:name), 'server', datetime('now'))
            ON CONFLICT(normalized_name) DO UPDATE SET
              updated_at=datetime('now')
        )"));
        qTag.bindValue(QStringLiteral(":name"), folderValue);
        qTag.exec();

        QSqlQuery qTagMap(database);
        qTagMap.prepare(QStringLiteral(R"(
            INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
            SELECT :account_email, :message_id, id, 'server', datetime('now')
            FROM tags
            WHERE normalized_name=lower(:label)
            ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
              observed_at=datetime('now')
        )"));
        qTagMap.bindValue(QStringLiteral(":account_email"), accountEmail);
        qTagMap.bindValue(QStringLiteral(":message_id"), messageId);
        qTagMap.bindValue(QStringLiteral(":label"), folderValue);
        qTagMap.exec();
    }

    // eM-inspired participant rows: per-message sender/recipient evidence.
    {
        QSqlQuery qDel(database);
        qDel.prepare(QStringLiteral("DELETE FROM message_participants WHERE account_email=:account_email AND message_id=:message_id"));
        qDel.bindValue(QStringLiteral(":account_email"), accountEmail);
        qDel.bindValue(QStringLiteral(":message_id"), messageId);
        qDel.exec();

        auto insertParticipant = [&](const QString &role,
                                     int position,
                                     const QString &displayName,
                                     const QString &address,
                                     const QString &source) {
            if (address.trimmed().isEmpty() && displayName.trimmed().isEmpty()) return;
            QSqlQuery qP(database);
            qP.prepare(QStringLiteral(R"(
                INSERT INTO message_participants (account_email, message_id, role, position, display_name, address, source)
                VALUES (:account_email, :message_id, :role, :position, :display_name, :address, :source)
                ON CONFLICT(account_email, message_id, role, position) DO UPDATE SET
                  display_name=excluded.display_name,
                  address=excluded.address,
                  source=excluded.source
            )"));
            qP.bindValue(QStringLiteral(":account_email"), accountEmail);
            qP.bindValue(QStringLiteral(":message_id"), messageId);
            qP.bindValue(QStringLiteral(":role"), role);
            qP.bindValue(QStringLiteral(":position"), position);
            qP.bindValue(QStringLiteral(":display_name"), displayName.trimmed());
            qP.bindValue(QStringLiteral(":address"), address.trimmed().toLower());
            qP.bindValue(QStringLiteral(":source"), source);
            qP.exec();
        };

        insertParticipant(QStringLiteral("sender"), 0, senderDisplayNameValue, senderEmailValue, QStringLiteral("header"));
        insertParticipant(QStringLiteral("recipient"), 0, recipientDisplayNameValue, recipientEmailValue, QStringLiteral("header"));
    }

    // Identity-level avatar cache store: one avatar file URL per normalized email.
    // Data URIs are written to disk; file:// URLs are stored in the DB.
    if (!senderEmailValue.isEmpty() && !avatarUrlValue.isEmpty()) {
        const QString storedSenderUrl = avatarUrlValue;
        if (!storedSenderUrl.isEmpty()) {
            QSqlQuery qAvatar(database);
            qAvatar.prepare(QStringLiteral(R"(
                INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
                VALUES (:email, :avatar_url, :source, datetime('now'), 0)
                ON CONFLICT(email) DO UPDATE SET
                  avatar_url=excluded.avatar_url,
                  source=CASE WHEN excluded.source IS NOT NULL AND length(trim(excluded.source))>0 THEN excluded.source ELSE contact_avatars.source END,
                  last_checked_at=datetime('now'),
                  failure_count=0
            )"));
            qAvatar.bindValue(QStringLiteral(":email"), senderEmailValue);
            qAvatar.bindValue(QStringLiteral(":avatar_url"), storedSenderUrl);
            qAvatar.bindValue(QStringLiteral(":source"), avatarSourceValue);
            qAvatar.exec();
            m_avatarCache.insert(senderEmailValue, storedSenderUrl);
        }
    }
    if (!recipientEmailValue.isEmpty() && !recipientAvatarUrlValue.isEmpty()) {
        const QString storedRecipientUrl = recipientAvatarUrlValue;
        if (!storedRecipientUrl.isEmpty()) {
            QSqlQuery qAvatar(database);
            qAvatar.prepare(QStringLiteral(R"(
                INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
                VALUES (:email, :avatar_url, 'google-people', datetime('now'), 0)
                ON CONFLICT(email) DO UPDATE SET
                  avatar_url=excluded.avatar_url,
                  source='google-people',
                  last_checked_at=datetime('now'),
                  failure_count=0
            )"));
            qAvatar.bindValue(QStringLiteral(":email"), recipientEmailValue);
            qAvatar.bindValue(QStringLiteral(":avatar_url"), storedRecipientUrl);
            qAvatar.exec();
            m_avatarCache.insert(recipientEmailValue, storedRecipientUrl);
        }
    } else if (!recipientEmailValue.isEmpty() && recipientAvatarLookupMiss) {
        QSqlQuery qAvatarMiss(database);
        qAvatarMiss.prepare(QStringLiteral(R"(
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
        )"));
        qAvatarMiss.bindValue(QStringLiteral(":email"), recipientEmailValue);
        qAvatarMiss.exec();
        m_avatarCache.remove(recipientEmailValue);  // CASE expression; evict to re-query
    }

    if (!senderEmailValue.isEmpty() && avatarUrlValue.isEmpty()) {
        // No avatar resolved for this sender — record a miss so we don't retry too soon.
        // Never store raw favicon URLs as fallback (they 404 without session cookies).
        QSqlQuery qAvatarMiss(database);
        qAvatarMiss.prepare(QStringLiteral(R"(
            INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
            VALUES (:email, '', 'lookup-miss', datetime('now'), 1)
            ON CONFLICT(email) DO UPDATE SET
              avatar_url=CASE
                           -- Preserve existing file:// URLs — they are fully resolved and good.
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
        )"));
        qAvatarMiss.bindValue(QStringLiteral(":email"), senderEmailValue);
        qAvatarMiss.exec();
        m_avatarCache.remove(senderEmailValue);  // CASE expression; evict to re-query
    }

    auto upsertDisplayName = [&](const QString &email, const QString &displayName, const QString &source) {
        const QString e = email.trimmed().toLower();
        const QString cand = displayName.trimmed();
        if (e.isEmpty() || cand.isEmpty()) return;

        QString existing;
        int existingScore = std::numeric_limits<int>::min() / 4;
        {
            QSqlQuery qCur(database);
            qCur.prepare(QStringLiteral("SELECT display_name, display_score FROM contact_display_names WHERE email=:email LIMIT 1"));
            qCur.bindValue(QStringLiteral(":email"), e);
            if (qCur.exec() && qCur.next()) {
                existing = qCur.value(0).toString().trimmed();
                existingScore = qCur.value(1).toInt();
            }
        }

        const int newScore = displayNameScoreForEmail(cand, e);
        const int oldScore = !existing.isEmpty() ? existingScore : (std::numeric_limits<int>::min() / 4);
        if (!existing.isEmpty() && newScore < oldScore) return;

        QSqlQuery qName(database);
        qName.prepare(QStringLiteral(R"(
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
        )"));
        qName.bindValue(QStringLiteral(":email"), e);
        qName.bindValue(QStringLiteral(":display_name"), cand);
        qName.bindValue(QStringLiteral(":source"), source);
        qName.bindValue(QStringLiteral(":display_score"), newScore);
        qName.exec();
    };

    upsertDisplayName(senderEmailValue, senderDisplayNameValue, QStringLiteral("sender-header"));
    if (!recipientEmailValue.isEmpty()
            && recipientEmailValue.compare(accountEmail.trimmed(), Qt::CaseInsensitive) != 0) {
        upsertDisplayName(recipientEmailValue, recipientDisplayNameValue, QStringLiteral("recipient-header"));
    }

    // Migration scaffold write path: persist observed Gmail labels with provenance.
    if (!rawGmailLabels.trimmed().isEmpty()) {
        const QRegularExpression tokenRe(QStringLiteral("\"([^\"]+)\"|([^\\s()]+)"));
        QRegularExpressionMatchIterator it = tokenRe.globalMatch(rawGmailLabels);
        while (it.hasNext()) {
            const auto m = it.next();
            QString label = m.captured(1).trimmed();
            if (label.isEmpty()) label = m.captured(2).trimmed();
            if (label.isEmpty()) continue;

            QSqlQuery qLabel(database);
            qLabel.prepare(QStringLiteral(R"(
                INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
                VALUES (:account_email, :message_id, :label, 'imap-label', 100, datetime('now'))
                ON CONFLICT(account_email, message_id, label) DO UPDATE SET
                  source='imap-label',
                  confidence=MAX(message_labels.confidence, 100),
                  observed_at=datetime('now')
            )"));
            qLabel.bindValue(QStringLiteral(":account_email"), accountEmail);
            qLabel.bindValue(QStringLiteral(":message_id"), messageId);
            qLabel.bindValue(QStringLiteral(":label"), label);
            qLabel.exec();

            // Unified tag projection (global tags + per-message mapping).
            QSqlQuery qTag(database);
            qTag.prepare(QStringLiteral(R"(
                INSERT INTO tags (name, normalized_name, origin, updated_at)
                VALUES (:name, lower(:name), 'server', datetime('now'))
                ON CONFLICT(normalized_name) DO UPDATE SET
                  updated_at=datetime('now')
            )"));
            qTag.bindValue(QStringLiteral(":name"), label);
            qTag.exec();

            QSqlQuery qTagMap(database);
            qTagMap.prepare(QStringLiteral(R"(
                INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
                SELECT :account_email, :message_id, id, 'server', datetime('now')
                FROM tags
                WHERE normalized_name=lower(:label)
                ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
                  observed_at=datetime('now')
            )"));
            qTagMap.bindValue(QStringLiteral(":account_email"), accountEmail);
            qTagMap.bindValue(QStringLiteral(":message_id"), messageId);
            qTagMap.bindValue(QStringLiteral(":label"), label);
            qTagMap.exec();
        }
    }

    // Protect direct Primary evidence: if Gmail labels explicitly reported Primary,
    // ensure a Primary label row exists (not a folder edge - categories are labels only).
    if (primaryLabelObserved) {
        const QString primaryLabel = QStringLiteral("[Gmail]/Categories/Primary");
        QSqlQuery qLabel(database);
        qLabel.prepare(QStringLiteral(R"(
            INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
            VALUES (:account_email, :message_id, :label, 'x-gm-labels-primary', 100, datetime('now'))
            ON CONFLICT(account_email, message_id, label) DO UPDATE SET
              source='x-gm-labels-primary',
              confidence=MAX(message_labels.confidence, 100),
              observed_at=datetime('now')
        )"));
        qLabel.bindValue(QStringLiteral(":account_email"), accountEmail);
        qLabel.bindValue(QStringLiteral(":message_id"), messageId);
        qLabel.bindValue(QStringLiteral(":label"), primaryLabel);
        qLabel.exec();

        QSqlQuery qTag(database);
        qTag.prepare(QStringLiteral(R"(
            INSERT INTO tags (name, normalized_name, origin, updated_at)
            VALUES (:name, lower(:name), 'server', datetime('now'))
            ON CONFLICT(normalized_name) DO UPDATE SET
              updated_at=datetime('now')
        )"));
        qTag.bindValue(QStringLiteral(":name"), primaryLabel);
        qTag.exec();

        QSqlQuery qTagMap(database);
        qTagMap.prepare(QStringLiteral(R"(
            INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
            SELECT :account_email, :message_id, id, 'server', datetime('now')
            FROM tags
            WHERE normalized_name=lower(:label)
            ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
              observed_at=datetime('now')
        )"));
        qTagMap.bindValue(QStringLiteral(":account_email"), accountEmail);
        qTagMap.bindValue(QStringLiteral(":message_id"), messageId);
        qTagMap.bindValue(QStringLiteral(":label"), primaryLabel);
        qTagMap.exec();
    }


    // Attachment metadata from BODYSTRUCTURE — stored on first sync pass.
    {
        const QVariantList attachments = header.value(QStringLiteral("attachments")).toList();
        if (!attachments.isEmpty())
            upsertAttachments(messageId, accountEmail, attachments);
    }

    // Server truth model: when a message is observed in Trash, remove stale membership
    // from non-trash folders for this account/message. It can be re-added later only if
    // the server reports it back in those folders.
    if (isTrashFolderName(folderValue)) {
        QList<QPair<QString, QString>> edgesToDelete;
        QSqlQuery qFindEdges(database);
        qFindEdges.prepare(QStringLiteral(R"(
            SELECT folder, uid
            FROM message_folder_map
            WHERE account_email=:account_email
              AND message_id=:message_id
              AND lower(folder) NOT IN ('trash','[gmail]/trash','[google mail]/trash')
              AND lower(folder) NOT LIKE '%/trash'
        )"));
        qFindEdges.bindValue(QStringLiteral(":account_email"), accountEmail);
        qFindEdges.bindValue(QStringLiteral(":message_id"), messageId);
        if (qFindEdges.exec()) {
            while (qFindEdges.next()) {
                edgesToDelete.append(qMakePair(qFindEdges.value(0).toString(), qFindEdges.value(1).toString()));
            }
        }
        for (const auto &edge : edgesToDelete) {
            deleteFolderEdge(database, accountEmail, edge.first, edge.second);
        }
    }

    scheduleReloadInbox();
}

QVariantList DataStore::inbox() const
{
    return m_inbox;
}

QVariantList DataStore::folders() const
{
    return m_folders;
}

QStringList DataStore::inboxCategoryTabs() const
{
    // Product policy for now: hard-limit visible Gmail category tabs to these three,
    // regardless of evidence in labels/message presence.
    return { QStringLiteral("Primary"), QStringLiteral("Promotions"), QStringLiteral("Social") };
}

QVariantList DataStore::tagItems() const
{
    QVariantList out;
    const auto database = db();
    if (!database.isValid() || !database.isOpen()) return out;

    QHash<QString, QVariantMap> byLabel;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT t.normalized_name AS label,
               COALESCE(NULLIF(trim(t.name), ''), t.normalized_name) AS display_name,
               COUNT(DISTINCT mtm.message_id) AS total,
               SUM(CASE WHEN EXISTS (
                    SELECT 1 FROM message_folder_map mfm
                    WHERE mfm.account_email=mtm.account_email
                      AND mfm.message_id=mtm.message_id
                      AND mfm.unread=1
               ) THEN 1 ELSE 0 END) AS unread,
               COALESCE(t.color, '') AS color
        FROM tags t
        LEFT JOIN message_tag_map mtm ON mtm.tag_id=t.id
        GROUP BY t.id
        ORDER BY t.normalized_name
    )"));
    if (q.exec()) {
        while (q.next()) {
            const QString label = q.value(0).toString().trimmed();
            if (isSystemLabelName(label) || isCategoryFolderName(label)) continue;

            QVariantMap row;
            row.insert(QStringLiteral("label"), label);
            row.insert(QStringLiteral("name"), q.value(1).toString().trimmed());
            row.insert(QStringLiteral("total"), q.value(2).toInt());
            row.insert(QStringLiteral("unread"), q.value(3).toInt());
            row.insert(QStringLiteral("color"), q.value(4).toString().trimmed());
            byLabel.insert(label, row);
        }
    }

    // Add Important as a tag (folder-backed, not label-backed)
    QSqlQuery qImportant(database);
    qImportant.prepare(QStringLiteral(R"(
        SELECT COUNT(DISTINCT message_id) AS total,
               SUM(CASE WHEN unread=1 THEN 1 ELSE 0 END) AS unread
        FROM message_folder_map
        WHERE lower(folder) LIKE '%/important'
    )"));
    if (qImportant.exec() && qImportant.next()) {
        const int total = qImportant.value(0).toInt();
        QVariantMap row;
        row.insert(QStringLiteral("label"), QStringLiteral("important"));
        row.insert(QStringLiteral("name"), QStringLiteral("Important"));
        row.insert(QStringLiteral("total"), total);
        row.insert(QStringLiteral("unread"), qImportant.value(1).toInt());
        row.insert(QStringLiteral("color"), QString());
        byLabel.insert(QStringLiteral("important"), row);
    } else {
        QVariantMap row;
        row.insert(QStringLiteral("label"), QStringLiteral("important"));
        row.insert(QStringLiteral("name"), QStringLiteral("Important"));
        row.insert(QStringLiteral("total"), 0);
        row.insert(QStringLiteral("unread"), 0);
        row.insert(QStringLiteral("color"), QString());
        byLabel.insert(QStringLiteral("important"), row);
    }

    // Fallback/union with top-level custom folders so Tags section isn't empty when
    // labels table is sparse.
    QSqlQuery qFolders(database);
    qFolders.prepare(QStringLiteral("SELECT lower(name), lower(flags) FROM folders"));
    if (qFolders.exec()) {
        while (qFolders.next()) {
            const QString name = qFolders.value(0).toString().trimmed();
            if (name.isEmpty()) continue;
            if (name == QStringLiteral("[google mail]")) continue;
            if (name.contains('/')) continue;

            if (isSystemLabelName(name) || isCategoryFolderName(name)) continue;
            if (byLabel.contains(name)) continue;

            QVariantMap row;
            row.insert(QStringLiteral("label"), name);
            row.insert(QStringLiteral("name"), name);
            row.insert(QStringLiteral("total"), 0);
            row.insert(QStringLiteral("unread"), 0);
            byLabel.insert(name, row);
        }
    }

    QStringList keys = byLabel.keys();
    std::sort(keys.begin(), keys.end());

    for (int i = 0; i < keys.size(); ++i) {
        const QString &k = keys.at(i);
        QVariantMap row = byLabel.value(k);

        QString color = row.value(QStringLiteral("color")).toString().trimmed();
        if (color.isEmpty()) {
            // Deterministic high-separation hues using golden-angle spacing.
            const int hue = (i * 137) % 360;
            QColor c;
            c.setHsv(hue, 190, 225);
            color = c.name(QColor::HexRgb);

            QSqlQuery qColor(database);
            qColor.prepare(QStringLiteral("UPDATE tags SET color=:color, updated_at=datetime('now') WHERE normalized_name=:name"));
            qColor.bindValue(QStringLiteral(":color"), color);
            qColor.bindValue(QStringLiteral(":name"), k);
            qColor.exec();
        }

        row.insert(QStringLiteral("color"), color);
        out.push_back(row);
    }
    return out;
}

void DataStore::upsertFolder(const QVariantMap &folder)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) return;
        database = db();
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        INSERT INTO folders (account_email, name, flags, special_use)
        VALUES (:account_email, :name, :flags, :special_use)
        ON CONFLICT(account_email, name) DO UPDATE SET
          flags = excluded.flags,
          special_use = excluded.special_use
    )"));

    q.bindValue(QStringLiteral(":account_email"), folder.value(QStringLiteral("accountEmail")));
    q.bindValue(QStringLiteral(":name"), folder.value(QStringLiteral("name")));
    q.bindValue(QStringLiteral(":flags"), folder.value(QStringLiteral("flags")));
    q.bindValue(QStringLiteral(":special_use"), folder.value(QStringLiteral("specialUse")));
    q.exec();

    reloadFolders();
}

void DataStore::pruneFolderToUids(const QString &accountEmail, const QString &folder, const QStringList &uids)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) return;
        database = db();
    }

    const QString acc = accountEmail.trimmed();
    const QString fld = folder.trimmed();
    if (acc.isEmpty() || fld.isEmpty()) return;

    // Guard rail: prevent accidental cross-namespace prune (e.g. All Mail UIDs applied to category folder).
    // This keeps destructive prune operations scoped to the folder namespace they were fetched from.
    // Also skip category folders - they're labels-only and have no folder edges to prune.
    const bool isCategoryFolder = folder.contains(QStringLiteral("/Categories/"), Qt::CaseInsensitive);
    if (isCategoryFolder) {
        qInfo().noquote() << "[prune-skip]" << "folder=" << folder << "reason=category-folders-are-labels-only";
        return;
    }
    if (isCategoryFolderName(fld) && !uids.isEmpty()) {
        const int existingCount = folderEdgeCount(database, acc, fld);
        const int overlapCount = folderOverlapCount(database, acc, fld, uids);
        const int allMailOverlap = allMailOverlapCount(database, acc, uids);
        const double overlapRatio = uids.isEmpty() ? 0.0 : static_cast<double>(overlapCount) / static_cast<double>(uids.size());
        const double allMailRatio = uids.isEmpty() ? 0.0 : static_cast<double>(allMailOverlap) / static_cast<double>(uids.size());

        const bool suspiciousZeroOverlap = existingCount >= 50 && overlapCount == 0;
        const bool suspiciousAllMailMirror = uids.size() >= 20 && allMailRatio >= 0.80 && overlapRatio <= 0.10;
        if (suspiciousZeroOverlap || suspiciousAllMailMirror) {
            qWarning().noquote() << "[prune-guard]"
                                 << "account=" << acc
                                 << "folder=" << fld
                                 << "incomingUidCount=" << uids.size()
                                 << "existingFolderRows=" << existingCount
                                 << "sameFolderOverlap=" << overlapCount
                                 << "allMailOverlap=" << allMailOverlap
                                 << "reason=" << (suspiciousAllMailMirror ? "allmail-namespace-mismatch" : "zero-overlap-namespace-mismatch");
            return;
        }
    }

    const int removedFolderRows = pruneFolderEdgesToUids(database, acc, fld, uids);

    // Clean orphan canonical rows.
    QSqlQuery qOrphan(database);
    qOrphan.exec(QStringLiteral("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"));
    const int removedCanonicalRows = qOrphan.numRowsAffected();

    qInfo().noquote() << "[prune]"
                      << "account=" << acc
                      << "folder=" << fld
                      << "remoteUidCount=" << uids.size()
                      << "removedFolderRows=" << removedFolderRows
                      << "removedCanonicalRows=" << removedCanonicalRows;

    if (removedFolderRows > 0 || removedCanonicalRows > 0)
        scheduleReloadInbox();
}

void DataStore::removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids,
                                             bool skipOrphanCleanup)
{
    if (uids.isEmpty()) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        if (!init()) return;
        database = db();
    }

    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) return;

    QStringList placeholders;
    placeholders.reserve(uids.size());
    for (int i = 0; i < uids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

    // Gmail assigns per-folder UIDs. Deletion detected from INBOX UID must fan out by
    // canonical message_id, not by the same UID in other folders.
    QList<QPair<QString, QString>> edgesToDelete;
    QSqlQuery qFind(database);
    qFind.prepare(QStringLiteral(R"(
        SELECT folder, uid
        FROM message_folder_map
        WHERE account_email=:account_email
          AND message_id IN (
                SELECT DISTINCT message_id
                FROM message_folder_map
                WHERE account_email=:account_email
                  AND uid IN (%1)
          )
          AND lower(folder) NOT IN ('trash','[gmail]/trash','[google mail]/trash')
          AND lower(folder) NOT LIKE '%/trash'
    )").arg(placeholders.join(QStringLiteral(","))));
    qFind.bindValue(QStringLiteral(":account_email"), acc);
    for (int i = 0; i < uids.size(); ++i) {
        qFind.bindValue(QStringLiteral(":u%1").arg(i), uids.at(i));
    }
    if (qFind.exec()) {
        while (qFind.next()) {
            edgesToDelete.append(qMakePair(qFind.value(0).toString(), qFind.value(1).toString()));
        }
    }
    int removedFolderRows = 0;
    for (const auto &edge : edgesToDelete) {
        removedFolderRows += deleteFolderEdge(database, acc, edge.first, edge.second);
    }

    int removedCanonicalRows = 0;
    if (!skipOrphanCleanup) {
        QSqlQuery qOrphan(database);
        qOrphan.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
        removedCanonicalRows = qOrphan.numRowsAffected();
    }

    qInfo().noquote() << "[prune-delete]"
                      << "account=" << acc
                      << "uidCount=" << uids.size()
                      << "removedFolderRows=" << removedFolderRows
                      << "removedCanonicalRows=" << removedCanonicalRows;

    if (removedFolderRows > 0 || removedCanonicalRows > 0)
        scheduleReloadInbox();
}

void DataStore::markMessageRead(const QString &accountEmail, const QString &uid)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty() || uid.trimmed().isEmpty()) return;

    // Mark unread=0 across ALL folder edges for this message, identified by any uid.
    // Gmail assigns per-folder UIDs (INBOX uid ≠ All Mail uid ≠ category uid), so we
    // fan out via message_id to cover every edge the display might pick as "preferred".

    QSqlQuery qMap(database);
    qMap.prepare(
        "UPDATE message_folder_map SET unread=0 "
        "WHERE account_email=:acc AND unread=1 AND message_id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND uid=:uid"
        ")"_L1);
    qMap.bindValue(":acc"_L1, acc);
    qMap.bindValue(":uid"_L1, uid);
    qMap.exec();

    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET unread=0 "
        "WHERE unread=1 AND id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND uid=:uid"
        ")"_L1);
    qMsg.bindValue(":acc"_L1, acc);
    qMsg.bindValue(":uid"_L1, uid);
    qMsg.exec();

    emit messageMarkedRead(acc, uid);
    if (qMsg.numRowsAffected() > 0)
        scheduleReloadInbox();
}

void DataStore::reconcileReadFlags(const QString &accountEmail, const QString &folder,
                                    const QStringList &readUids)
{
    // Batch flag reconciliation: marks unread=0 for UIDs the server confirms are read.
    // Only ever marks read — never marks a locally-read message unread.
    // Gmail per-folder UIDs: fan out through message_id so all edges of a message are updated.
    if (readUids.isEmpty()) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) return;

    QStringList placeholders;
    placeholders.reserve(readUids.size());
    for (int i = 0; i < readUids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

    // Step 1: update all folder edges for affected message_ids.
    QSqlQuery qMap(database);
    qMap.prepare(
        "UPDATE message_folder_map SET unread=0 "
        "WHERE account_email=:acc AND unread=1 AND message_id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));
    qMap.bindValue(":acc"_L1,    acc);
    qMap.bindValue(":folder"_L1, folder);
    for (int i = 0; i < readUids.size(); ++i)
        qMap.bindValue(QStringLiteral(":u%1").arg(i), readUids.at(i));
    qMap.exec();

    // Step 2: mirror into canonical messages table.
    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET unread=0 "
        "WHERE unread=1 AND id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));
    qMsg.bindValue(":acc"_L1,    acc);
    qMsg.bindValue(":folder"_L1, folder);
    for (int i = 0; i < readUids.size(); ++i)
        qMsg.bindValue(QStringLiteral(":u%1").arg(i), readUids.at(i));
    qMsg.exec();

    const int edgesUpdated = qMap.numRowsAffected();
    if (edgesUpdated > 0) {
        qInfo().noquote() << "[reconcile-flags]" << "acc=" << acc
                          << "folder=" << folder
                          << "readUids=" << readUids.size()
                          << "edgesUpdated=" << edgesUpdated;
        scheduleReloadInbox();
    }
}

QVariantMap DataStore::folderMapRowForEdge(const QString &accountEmail,
                                            const QString &folder,
                                            const QString &uid) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};
    QSqlQuery q(database);
    q.prepare("SELECT message_id, unread FROM message_folder_map "
              "WHERE account_email=:acc AND folder=:folder AND uid=:uid LIMIT 1"_L1);
    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":uid"_L1,    uid);
    if (!q.exec() || !q.next()) return {};
    QVariantMap row;
    row.insert("messageId"_L1, q.value(0).toLongLong());
    row.insert("unread"_L1,    q.value(1).toInt());
    return row;
}

void DataStore::deleteSingleFolderEdge(const QString &accountEmail,
                                        const QString &folder,
                                        const QString &uid)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const int removed = deleteFolderEdge(database, accountEmail, folder, uid);
    // Remove any messages now orphaned (no remaining folder edges).
    QSqlQuery q(database);
    q.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
    if (removed > 0 || q.numRowsAffected() > 0)
        scheduleReloadInbox();
}

void DataStore::insertFolderEdge(const QString &accountEmail, qint64 messageId,
                                  const QString &folder, const QString &uid, int unread)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO message_folder_map
            (account_email, message_id, folder, uid, unread, source, confidence, observed_at)
        VALUES (:acc, :mid, :folder, :uid, :unread, 'imap-label', 100, datetime('now'))
        ON CONFLICT(account_email, folder, uid) DO UPDATE SET
          message_id=excluded.message_id,
          unread=MIN(message_folder_map.unread, excluded.unread),
          source='imap-label', confidence=100, observed_at=datetime('now')
    )"_L1);
    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":mid"_L1,    messageId);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":uid"_L1,    uid);
    q.bindValue(":unread"_L1, unread);
    q.exec();
    // Clean up any orphaned messages now that this edge is committed.
    // The just-inserted edge protects its own message_id; only truly stale rows are removed.
    QSqlQuery qOrphan(database);
    qOrphan.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
    scheduleReloadInbox();
}

void DataStore::removeAllEdgesForMessageId(const QString &accountEmail, qint64 messageId)
{
    if (messageId <= 0) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    QSqlQuery q(database);
    q.prepare("DELETE FROM message_folder_map WHERE account_email=:acc AND message_id=:mid"_L1);
    q.bindValue(":acc"_L1, accountEmail);
    q.bindValue(":mid"_L1, messageId);
    q.exec();
    const int removedEdges = q.numRowsAffected();
    QSqlQuery qOrphan(database);
    qOrphan.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
    if (removedEdges > 0 || qOrphan.numRowsAffected() > 0)
        scheduleReloadInbox();
}

QStringList DataStore::folderUids(const QString &accountEmail, const QString &folder) const
{
    QStringList out;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    if (!q.exec()) return out;
    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty()) out.push_back(uid);
    }
    return out;
}

QStringList DataStore::folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const
{
    QStringList out;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return out;
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id "
        "WHERE mfm.account_email = :account_email AND lower(mfm.folder) = lower(:folder) "
        "AND (m.snippet IS NULL OR m.snippet = '')"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    if (!q.exec()) return out;
    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty()) out.push_back(uid);
    }
    return out;
}

qint64 DataStore::folderMaxUid(const QString &accountEmail, const QString &folder) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        return 0;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    if (!q.exec()) return 0;

    qint64 maxUid = 0;
    while (q.next()) {
        bool ok = false;
        const qint64 v = q.value(0).toString().toLongLong(&ok);
        if (ok && v > maxUid) maxUid = v;
    }
    return maxUid;
}

qint64 DataStore::folderMessageCount(const QString &accountEmail, const QString &folder) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return 0;
    return folderEdgeCount(database, accountEmail, folder);
}

QVariantMap DataStore::folderSyncStatus(const QString &accountEmail, const QString &folder) const
{
    QVariantMap out;

    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return out;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT uid_next, highest_modseq, messages
        FROM folder_sync_status
        WHERE account_email=:account_email
          AND lower(folder)=lower(:folder)
        LIMIT 1
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    if (!q.exec() || !q.next())
        return out;

    out.insert(QStringLiteral("uidNext"), q.value(0).toLongLong());
    out.insert(QStringLiteral("highestModSeq"), q.value(1).toLongLong());
    out.insert(QStringLiteral("messages"), q.value(2).toLongLong());
    return out;
}

void DataStore::upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                       const qint64 uidNext, const qint64 highestModSeq, const qint64 messages)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        INSERT INTO folder_sync_status(account_email, folder, uid_next, highest_modseq, messages, updated_at)
        VALUES(:account_email, :folder, :uid_next, :highest_modseq, :messages, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          uid_next=excluded.uid_next,
          highest_modseq=excluded.highest_modseq,
          messages=excluded.messages,
          updated_at=datetime('now')
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":uid_next"), uidNext);
    q.bindValue(QStringLiteral(":highest_modseq"), highestModSeq);
    q.bindValue(QStringLiteral(":messages"), messages);
    q.exec();
}

QStringList DataStore::bodyFetchCandidates(const QString &accountEmail, const QString &folder,
                                           const int limit) const
{
    QStringList out;

    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return out;

    const int boundedLimit = qBound(1, limit, 100);

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT mfm.uid
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND lower(mfm.folder)=lower(:folder)
          AND (
              m.body_html IS NULL
              OR trim(m.body_html) = ''
              OR lower(m.body_html) LIKE '%ok success [throttled]%'
              OR lower(m.body_html) LIKE '%authenticationfailed%'
          )
          AND datetime(m.received_at) >= datetime('now', '-3 months')
        ORDER BY datetime(m.received_at) DESC, m.id DESC
        LIMIT :limit
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":limit"), boundedLimit);

    if (!q.exec())
        return out;

    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty())
            out.push_back(uid);
    }

    return out;
}

QVariantList DataStore::bodyFetchCandidatesByAccount(const QString &accountEmail, const int limit) const
{
    QVariantList out;

    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return out;

    const int boundedLimit = qBound(1, limit, 100);

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT m.id AS message_id, mfm.folder, mfm.uid
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND (
              m.body_html IS NULL
              OR trim(m.body_html) = ''
              OR lower(m.body_html) LIKE '%ok success [throttled]%'
              OR lower(m.body_html) LIKE '%authenticationfailed%'
          )
          AND datetime(m.received_at) >= datetime('now', '-3 months')
        ORDER BY CASE WHEN lower(mfm.folder)='inbox' THEN 0 ELSE 1 END,
                 datetime(m.received_at) DESC,
                 m.id DESC
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());

    if (!q.exec())
        return out;

    QSet<qint64> seenMessageIds;
    while (q.next()) {
        const qint64 messageId = q.value(0).toLongLong();
        if (seenMessageIds.contains(messageId))
            continue;
        seenMessageIds.insert(messageId);

        QVariantMap row;
        row.insert(QStringLiteral("messageId"), messageId);
        row.insert(QStringLiteral("folder"), q.value(1).toString());
        row.insert(QStringLiteral("uid"), q.value(2).toString());
        out.push_back(row);

        if (out.size() >= boundedLimit)
            break;
    }

    if (!out.isEmpty())
        return out;

    // Fallback if received_at parsing/filtering excludes everything: keep Inbox-first
    // ordering and dedupe by message id, but without time window.
    QSqlQuery qFallback(database);
    qFallback.prepare(QStringLiteral(R"(
        SELECT m.id AS message_id, mfm.folder, mfm.uid
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND (
              m.body_html IS NULL
              OR trim(m.body_html) = ''
              OR lower(m.body_html) LIKE '%ok success [throttled]%'
              OR lower(m.body_html) LIKE '%authenticationfailed%'
          )
        ORDER BY CASE WHEN lower(mfm.folder)='inbox' THEN 0 ELSE 1 END,
                 datetime(m.received_at) DESC,
                 m.id DESC
    )"));
    qFallback.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());

    if (!qFallback.exec())
        return out;

    seenMessageIds.clear();
    while (qFallback.next()) {
        const qint64 messageId = qFallback.value(0).toLongLong();
        if (seenMessageIds.contains(messageId))
            continue;
        seenMessageIds.insert(messageId);

        QVariantMap row;
        row.insert(QStringLiteral("messageId"), messageId);
        row.insert(QStringLiteral("folder"), qFallback.value(1).toString());
        row.insert(QStringLiteral("uid"), qFallback.value(2).toString());
        out.push_back(row);

        if (out.size() >= boundedLimit)
            break;
    }

    return out;
}

QVariantList DataStore::fetchCandidatesForMessageKey(const QString &accountEmail,
                                                      const QString &folder,
                                                      const QString &uid) const
{
    QVariantList out;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    // UID is mailbox-scoped; resolve message_id from the exact (account, folder, uid) edge first.
    QSqlQuery qMid(database);
    qMid.prepare(QStringLiteral(R"(
        SELECT message_id
        FROM message_folder_map
        WHERE account_email=:account_email
          AND lower(folder)=lower(:folder)
          AND uid=:uid
        LIMIT 1
    )"));
    qMid.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    qMid.bindValue(QStringLiteral(":folder"), folder.trimmed());
    qMid.bindValue(QStringLiteral(":uid"), uid.trimmed());
    if (!qMid.exec() || !qMid.next()) {
        QVariantMap fallback;
        fallback.insert(QStringLiteral("folder"), folder.trimmed());
        fallback.insert(QStringLiteral("uid"), uid.trimmed());
        out.push_back(fallback);
        return out;
    }

    const qint64 messageId = qMid.value(0).toLongLong();
    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT folder, uid
        FROM message_folder_map
        WHERE account_email=:account_email
          AND message_id=:message_id
        ORDER BY CASE
            WHEN lower(folder)=lower(:requested_folder) THEN 0
            WHEN lower(folder)='inbox' OR lower(folder)='[gmail]/inbox' OR lower(folder)='[google mail]/inbox' OR lower(folder) LIKE '%/inbox' THEN 1
            WHEN lower(folder) LIKE '%/categories/%' THEN 2
            WHEN lower(folder)='[gmail]/all mail' OR lower(folder)='[google mail]/all mail' OR lower(folder) LIKE '%/all mail' THEN 3
            ELSE 4
        END,
        folder
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":message_id"), messageId);
    q.bindValue(QStringLiteral(":requested_folder"), folder.trimmed());
    if (!q.exec()) return out;
    while (q.next()) {
        const QString f = q.value(0).toString().trimmed();
        const QString u = q.value(1).toString().trimmed();
        if (f.isEmpty() || u.isEmpty()) continue;
        QVariantMap row;
        row.insert(QStringLiteral("folder"), f);
        row.insert(QStringLiteral("uid"), u);
        out.push_back(row);
    }
    return out;
}

bool DataStore::hasUsableBodyForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return false;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT m.body_html
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND lower(mfm.folder)=lower(:folder)
          AND mfm.uid=:uid
        LIMIT 1
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":uid"), uid.trimmed());

    if (!q.exec() || !q.next())
        return false;

    const QString html = q.value(0).toString();
    if (html.trimmed().isEmpty())
        return false;

    const QString lower = html.toLower();
    if (lower.contains("ok success [throttled]"_L1) || lower.contains("authenticationfailed"_L1))
        return false;

    // Treat unresolved CID-backed inline images as not-yet-hydrated content.
    // This allows a follow-up full FETCH+parse pass to inline embedded resources
    // (common in Drafts where body_html may initially contain src="cid:...").
    if (lower.contains("cid:"_L1))
        return false;

    // Detect structurally truncated HTML: more </table> than <table> means the body
    // was cut mid-content (e.g. by the 128 KB partial IMAP fetch window).
    const int tableOpen  = lower.count("<table"_L1);
    const int tableClose = lower.count("</table>"_L1);
    if (tableClose > tableOpen)
        return false;

    return true;
}

QVariantMap DataStore::messageByKey(const QString &accountEmail, const QString &folder, const QString &uid) const
{
    QVariantMap row;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return row;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT mfm.account_email,
               mfm.folder,
               mfm.uid,
               cm.id,
               cm.sender,
               cm.subject,
               cm.recipient,
               '' as recipient_avatar_url,
               cm.received_at,
               cm.snippet,
               cm.body_html,
               '' as avatar_domain,
               '' as avatar_url,
               '' as avatar_source,
               mfm.unread,
               COALESCE(cm.list_unsubscribe, '')   as list_unsubscribe,
               COALESCE(cm.reply_to, '')            as reply_to,
               COALESCE(cm.return_path, '')         as return_path,
               COALESCE(cm.auth_results, '')        as auth_results,
               COALESCE(cm.x_mailer, '')            as x_mailer,
               COALESCE(cm.in_reply_to, '')         as in_reply_to,
               COALESCE(cm.references_header, '')   as references_header,
               COALESCE(cm.esp_vendor, '')          as esp_vendor,
               COALESCE(cm.thread_id, '')           as thread_id,
               COALESCE((
                 SELECT COUNT(DISTINCT m2.id) FROM messages m2
                 WHERE m2.account_email = cm.account_email
                   AND m2.thread_id = cm.thread_id
                   AND cm.thread_id IS NOT NULL
                   AND length(trim(COALESCE(cm.thread_id, ''))) > 0
               ), 1)                                as thread_count
        FROM message_folder_map mfm
        JOIN messages cm ON cm.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND lower(mfm.folder)=lower(:folder)
          AND mfm.uid=:uid
        LIMIT 1
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":uid"), uid.trimmed());
    if (!q.exec() || !q.next()) return row;

    row.insert(QStringLiteral("accountEmail"), q.value(0));
    row.insert(QStringLiteral("folder"), q.value(1));
    row.insert(QStringLiteral("uid"), q.value(2));
    row.insert(QStringLiteral("messageId"), q.value(3));
    row.insert(QStringLiteral("sender"), q.value(4));
    row.insert(QStringLiteral("subject"), q.value(5));
    row.insert(QStringLiteral("recipient"), q.value(6));
    row.insert(QStringLiteral("recipientAvatarUrl"), q.value(7));
    row.insert(QStringLiteral("receivedAt"), q.value(8));
    row.insert(QStringLiteral("snippet"), q.value(9));
    row.insert(QStringLiteral("bodyHtml"), q.value(10));
    row.insert(QStringLiteral("avatarDomain"), q.value(11));
    row.insert(QStringLiteral("avatarUrl"), q.value(12));
    row.insert(QStringLiteral("avatarSource"), q.value(13));
    row.insert(QStringLiteral("unread"),          q.value(14).toInt() == 1);
    row.insert(QStringLiteral("listUnsubscribe"), q.value(15).toString());
    row.insert(QStringLiteral("replyTo"),         q.value(16).toString());
    row.insert(QStringLiteral("returnPath"),      q.value(17).toString());
    row.insert(QStringLiteral("authResults"),     q.value(18).toString());
    row.insert(QStringLiteral("xMailer"),         q.value(19).toString());
    row.insert(QStringLiteral("inReplyTo"),       q.value(20).toString());
    row.insert(QStringLiteral("references"),      q.value(21).toString());
    row.insert(QStringLiteral("espVendor"),       q.value(22).toString());
    row.insert(QStringLiteral("threadId"),        q.value(23).toString());
    row.insert(QStringLiteral("threadCount"),     q.value(24).toInt());
    return row;
}

QVariantList DataStore::messagesForThread(const QString &accountEmail, const QString &threadId) const
{
    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen() || threadId.trimmed().isEmpty())
        return {};

    // One row per logical message (GROUP BY cm.id), ordered oldest-first.
    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT mfm.account_email,
               mfm.folder,
               mfm.uid,
               cm.id,
               cm.sender,
               cm.subject,
               cm.recipient,
               cm.received_at,
               cm.snippet,
               cm.body_html,
               cm.avatar_domain,
               cm.avatar_url,
               cm.avatar_source,
               mfm.unread,
               COALESCE(cm.thread_id, '') as thread_id,
               COALESCE(cm.reply_to, '')  as reply_to
        FROM messages cm
        JOIN message_folder_map mfm ON mfm.message_id = cm.id
          AND mfm.account_email = cm.account_email
        WHERE cm.account_email = :account_email
          AND cm.thread_id = :thread_id
        GROUP BY cm.id
        ORDER BY cm.received_at ASC
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":thread_id"),     threadId.trimmed());
    if (!q.exec()) return {};

    QVariantList result;
    QSet<int> seen;
    while (q.next()) {
        const int msgId = q.value(3).toInt();
        if (seen.contains(msgId)) continue;
        seen.insert(msgId);
        QVariantMap row;
        row[QStringLiteral("accountEmail")] = q.value(0).toString();
        row[QStringLiteral("folder")]       = q.value(1).toString();
        row[QStringLiteral("uid")]          = q.value(2).toString();
        row[QStringLiteral("messageId")]    = msgId;
        row[QStringLiteral("sender")]       = q.value(4).toString();
        row[QStringLiteral("subject")]      = q.value(5).toString();
        row[QStringLiteral("recipient")]    = q.value(6).toString();
        row[QStringLiteral("receivedAt")]   = q.value(7).toString();
        row[QStringLiteral("snippet")]      = q.value(8).toString();
        row[QStringLiteral("bodyHtml")]     = q.value(9).toString();
        row[QStringLiteral("avatarDomain")] = q.value(10).toString();
        row[QStringLiteral("avatarUrl")]    = q.value(11).toString();
        row[QStringLiteral("avatarSource")] = q.value(12).toString();
        row[QStringLiteral("unread")]       = q.value(13).toInt() == 1;
        row[QStringLiteral("threadId")]     = q.value(14).toString();
        row[QStringLiteral("replyTo")]      = q.value(15).toString();
        result.push_back(row);
    }
    return result;
}

bool DataStore::isSenderTrusted(const QString &domain) const
{
    if (domain.trimmed().isEmpty()) return false;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM sender_image_permissions WHERE domain=:domain LIMIT 1"));
    q.bindValue(QStringLiteral(":domain"), domain.trimmed().toLower());
    return q.exec() && q.next();
}

void DataStore::setTrustedSenderDomain(const QString &domain)
{
    const QString d = domain.trimmed().toLower();
    if (d.isEmpty()) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO sender_image_permissions (domain) VALUES (:domain) ON CONFLICT(domain) DO NOTHING"));
    q.bindValue(QStringLiteral(":domain"), d);
    q.exec();
}

bool DataStore::updateBodyForKey(const QString &accountEmail,
                                 const QString &folder,
                                 const QString &uid,
                                 const QString &bodyHtml)
{
    const QString html = bodyHtml.trimmed();
    if (html.isEmpty()) return false;

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return false;

    int prevLen = -1;
    int prevTP = 0;
    QString senderForTP;
    {
        QSqlQuery qPrev(database);
        qPrev.prepare(QStringLiteral(R"(
            SELECT length(trim(COALESCE(m.body_html, ''))), m.sender, COALESCE(m.has_tracking_pixel, 0)
            FROM messages m
            WHERE m.id = (
                SELECT mfm.message_id
                FROM message_folder_map mfm
                WHERE mfm.account_email=:account_email
                  AND lower(mfm.folder)=lower(:folder)
                  AND mfm.uid=:uid
                LIMIT 1
            )
        )"));
        qPrev.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
        qPrev.bindValue(QStringLiteral(":folder"), folder.trimmed());
        qPrev.bindValue(QStringLiteral(":uid"), uid.trimmed());
        if (qPrev.exec() && qPrev.next()) {
            prevLen = qPrev.value(0).toInt();
            senderForTP = qPrev.value(1).toString();
            prevTP = qPrev.value(2).toInt();
        }
    }

    const int hasTP = computeHasTrackingPixel(html, senderForTP) ? 1 : 0;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        UPDATE messages
        SET body_html = CASE
                          WHEN :body_html IS NOT NULL
                               AND length(trim(:body_html)) > length(trim(COALESCE(body_html, '')))
                          THEN :body_html
                          ELSE body_html
                        END,
            has_tracking_pixel = :has_tp
        WHERE id = (
            SELECT mfm.message_id
            FROM message_folder_map mfm
            WHERE mfm.account_email=:account_email
              AND lower(mfm.folder)=lower(:folder)
              AND mfm.uid=:uid
            LIMIT 1
        )
    )"));
    q.bindValue(QStringLiteral(":body_html"), html);
    q.bindValue(QStringLiteral(":has_tp"), hasTP);
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":uid"), uid.trimmed());
    if (!q.exec()) return false;

    const bool changed = q.numRowsAffected() > 0;

    const QString htmlLower = html.left(1024).toLower();
    const bool hasHtmlish = QRegularExpression(QStringLiteral("<html|<body|<div|<table|<p|<br|<span|<img|<a\\b"),
                                             QRegularExpression::CaseInsensitiveOption).match(html).hasMatch();
    const bool hasMarkdownLinks = QRegularExpression(QStringLiteral("\\[[^\\]\\n]{1,240}\\]\\(https?://[^\\s)]+\\)"),
                                                     QRegularExpression::CaseInsensitiveOption).match(html).hasMatch();
    const bool hasMimeHeaders = htmlLower.contains(QStringLiteral("content-type:"))
                             || htmlLower.contains(QStringLiteral("mime-version:"));
    const bool suspicious = !hasHtmlish || hasMarkdownLinks || hasMimeHeaders || html.size() < 160;

    if (suspicious || !changed) {
        qWarning().noquote() << "[hydrate-html-db] store-write"
                             << "account=" << accountEmail.trimmed()
                             << "folder=" << folder.trimmed()
                             << "uid=" << uid.trimmed()
                             << "prevLen=" << prevLen
                             << "newLen=" << html.size()
                             << "changed=" << changed
                             << "hasHtmlish=" << hasHtmlish
                             << "hasMarkdownLinks=" << hasMarkdownLinks
                             << "hasMimeHeaders=" << hasMimeHeaders;
    }

    // Only reload the list when the tracking-pixel flag changed — that flag IS
    // part of the list model (MessageCard shows an indicator). body_html is NOT
    // a list model role; QML bindings that need the fresh body listen to
    // bodyHtmlUpdated instead of waiting for an inbox reload.
    if (changed && hasTP != prevTP)
        scheduleReloadInbox();

    if (changed)
        emit bodyHtmlUpdated(accountEmail.trimmed(), folder.trimmed(), uid.trimmed());

    return changed;
}

void DataStore::scheduleReloadInbox()
{
    if (m_reloadInboxScheduled)
        return;

    m_reloadInboxScheduled = true;
    QTimer::singleShot(300, this, [this]() {
        m_reloadInboxScheduled = false;
        reloadInbox();
    });
}

void DataStore::reloadInbox()
{
    m_inbox.clear();
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        emit inboxChanged();
        return;
    }

    QSqlQuery q(database);
    q.exec(QStringLiteral(R"(
      SELECT mfm.account_email,
             mfm.folder,
             mfm.uid,
             cm.id,
             cm.sender,
             cm.subject,
             cm.recipient,
             '' as recipient_avatar_url,
             cm.received_at,
             cm.snippet,
             '' as avatar_domain,
             '' as avatar_url,
             '' as avatar_source,
             mfm.unread,
             COALESCE(cm.thread_id, '') as thread_id,
             COALESCE((
               SELECT COUNT(DISTINCT m2.id) FROM messages m2
               WHERE m2.account_email = cm.account_email
                 AND m2.thread_id = cm.thread_id
                 AND cm.thread_id IS NOT NULL
                 AND length(trim(COALESCE(cm.thread_id, ''))) > 0
             ), 1) as thread_count,
             COALESCE(cm.gm_thr_id, '') as gm_thr_id,
             COALESCE(cm.has_tracking_pixel, 0) as has_tracking_pixel
      FROM message_folder_map mfm
      JOIN messages cm ON cm.id = mfm.message_id
      ORDER BY cm.received_at DESC
      LIMIT 500
    )"));

    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("accountEmail"), q.value(0));
        row.insert(QStringLiteral("folder"), q.value(1));
        row.insert(QStringLiteral("uid"), q.value(2));
        row.insert(QStringLiteral("messageId"), q.value(3));
        row.insert(QStringLiteral("sender"), q.value(4));
        row.insert(QStringLiteral("subject"), q.value(5));
        row.insert(QStringLiteral("recipient"), q.value(6));
        row.insert(QStringLiteral("recipientAvatarUrl"), q.value(7));
        row.insert(QStringLiteral("receivedAt"), q.value(8));
        row.insert(QStringLiteral("snippet"), q.value(9));
        row.insert(QStringLiteral("avatarDomain"), q.value(10));
        row.insert(QStringLiteral("avatarUrl"), q.value(11));
        row.insert(QStringLiteral("avatarSource"), q.value(12));
        row.insert(QStringLiteral("unread"), q.value(13).toInt() == 1);
        row.insert(QStringLiteral("threadId"),         q.value(14).toString());
        row.insert(QStringLiteral("threadCount"),      q.value(15).toInt());
        row.insert(QStringLiteral("gmThrId"),          q.value(16).toString());
        row.insert(QStringLiteral("hasTrackingPixel"), q.value(17).toInt() == 1);
        m_inbox.push_back(row);
    }

    // Deduplicate by thread: the SQL orders newest-first, so the first occurrence
    // of each thread_id is the latest message — keep it and discard older siblings.
    {
        QSet<QString> seenThreads;
        QVariantList deduped;
        deduped.reserve(m_inbox.size());
        for (const QVariant &v : m_inbox) {
            const QVariantMap r  = v.toMap();
            const QString tid    = r.value(QStringLiteral("threadId")).toString().trimmed();
            QString gtid         = r.value(QStringLiteral("gmThrId")).toString().trimmed();
            const QString acct   = r.value(QStringLiteral("accountEmail")).toString();
            if (gtid.isEmpty() && tid.startsWith(QStringLiteral("gm:"), Qt::CaseInsensitive))
                gtid = tid.mid(3).trimmed();
            const QString key    = !gtid.isEmpty()
                ? acct + QStringLiteral("|gtid:") + gtid
                : (tid.isEmpty()
                    ? acct + QStringLiteral("|msg:") + r.value(QStringLiteral("messageId")).toString()
                    : acct + QStringLiteral("|tid:") + tid);
            if (seenThreads.contains(key)) continue;
            seenThreads.insert(key);
            deduped.push_back(v);
        }
        m_inbox = deduped;
    }

    emit inboxChanged();
}

QVariantList DataStore::messagesForSelection(const QString &folderKey,
                                             const QStringList &selectedCategories,
                                             int selectedCategoryIndex,
                                             int limit,
                                             int offset,
                                             bool *hasMore) const
{
    const QSqlDatabase database = db();
    QVariantList rows = m_inbox;
    const QString key = folderKey.trimmed();
    if (hasMore) *hasMore = false;

    QSet<QString> trashedMessageIds;
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        if (isTrashFolderName(r.value(QStringLiteral("folder")).toString())) {
            trashedMessageIds.insert(r.value(QStringLiteral("messageId")).toString());
        }
    }

    auto dedupeByMessageKey = [](const QVariantList &in) {
        QVariantList out;
        QSet<QString> seen;
        for (const QVariant &v : in) {
            const QVariantMap r = v.toMap();
            const QString k = r.value(QStringLiteral("accountEmail")).toString() + "|"
                              + r.value(QStringLiteral("sender")).toString() + "|"
                              + r.value(QStringLiteral("subject")).toString() + "|"
                              + r.value(QStringLiteral("receivedAt")).toString();
            if (seen.contains(k)) continue;
            seen.insert(k);
            out.push_back(v);
        }
        return out;
    };

    auto annotateMessageFlags = [&](QVariantList &list) {
        if (list.isEmpty())
            return;

        QStringList messageIds;
        messageIds.reserve(list.size());
        for (const QVariant &v : list) {
            const QString mid = v.toMap().value(QStringLiteral("messageId")).toString();
            if (!mid.isEmpty())
                messageIds.push_back(mid);
        }

        // Batch-check which messages are important (server folder edge or local label).
        QSet<QString> importantMessageIds;
        if (database.isValid() && database.isOpen() && !messageIds.isEmpty()) {
            QStringList placeholders2;
            placeholders2.reserve(messageIds.size());
            for (int i = 0; i < messageIds.size(); ++i)
                placeholders2 << QStringLiteral(":m%1").arg(i);
            const QString inClause = placeholders2.join(QLatin1Char(','));
            // Server-synced
            QSqlQuery qImp(database);
            qImp.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_folder_map WHERE lower(folder) LIKE '%/important' AND message_id IN (%1)").arg(inClause));
            for (int i = 0; i < messageIds.size(); ++i)
                qImp.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qImp.exec()) {
                while (qImp.next()) importantMessageIds.insert(qImp.value(0).toString());
            }
            // Locally toggled
            QSqlQuery qImpL(database);
            qImpL.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)='important' AND message_id IN (%1)").arg(inClause));
            for (int i = 0; i < messageIds.size(); ++i)
                qImpL.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qImpL.exec()) {
                while (qImpL.next()) importantMessageIds.insert(qImpL.value(0).toString());
            }
        }

        QSet<QString> withAttachments;
        if (database.isValid() && database.isOpen() && !messageIds.isEmpty()) {
            QStringList placeholders;
            placeholders.reserve(messageIds.size());
            for (int i = 0; i < messageIds.size(); ++i)
                placeholders << QStringLiteral(":m%1").arg(i);

            QSqlQuery q(database);
            q.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_attachments WHERE message_id IN (%1)").arg(placeholders.join(',')));
            for (int i = 0; i < messageIds.size(); ++i)
                q.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));

            if (q.exec()) {
                while (q.next())
                    withAttachments.insert(q.value(0).toString());
            }
        }

        for (int i = 0; i < list.size(); ++i) {
            QVariantMap row = list.at(i).toMap();
            const QString mid = row.value(QStringLiteral("messageId")).toString();
            row.insert(QStringLiteral("hasAttachments"),    withAttachments.contains(mid));
            row.insert(QStringLiteral("isImportant"),       importantMessageIds.contains(mid));
            row.insert(QStringLiteral("hasTrackingPixel"),  row.value(QStringLiteral("hasTrackingPixel")).toBool());
            list[i] = row;
        }
    };

    auto pageRows = [&](const QVariantList &in) {
        const int safeOffset = qMax(0, offset);
        if (limit <= 0) {
            if (hasMore) *hasMore = false;
            QVariantList out = in;
            annotateMessageFlags(out);
            return out;
        }
        const int total = in.size();
        if (safeOffset >= total) {
            if (hasMore) *hasMore = false;
            return QVariantList{};
        }
        const int end = qMin(total, safeOffset + limit);
        QVariantList out;
        out.reserve(end - safeOffset);
        for (int i = safeOffset; i < end; ++i) out.push_back(in.at(i));
        if (hasMore) *hasMore = (end < total);
        annotateMessageFlags(out);
        return out;
    };

    if (key.startsWith(QStringLiteral("local:"), Qt::CaseInsensitive)
        || key.compare(QStringLiteral("favorites:flagged"), Qt::CaseInsensitive) == 0) {
        if (hasMore) *hasMore = false;
        return {};
    }

    if (key.compare(QStringLiteral("favorites:all-inboxes"), Qt::CaseInsensitive) == 0) {
        QVariantList visible;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            if (trashedMessageIds.contains(r.value(QStringLiteral("messageId")).toString())) continue;
            visible.push_back(v);
        }
        return pageRows(dedupeByMessageKey(visible));
    }

    if (key.compare(QStringLiteral("favorites:unread"), Qt::CaseInsensitive) == 0) {
        QVariantList unread;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            if (trashedMessageIds.contains(r.value(QStringLiteral("messageId")).toString())) continue;
            if (r.value(QStringLiteral("unread")).toBool()) unread.push_back(v);
        }
        return pageRows(dedupeByMessageKey(unread));
    }

    QString selectedFolder;
    QString selectedTag;
    if (key.startsWith(QStringLiteral("account:"), Qt::CaseInsensitive)) {
        selectedFolder = key.mid(QStringLiteral("account:").size()).toLower();
    } else if (key.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive)) {
        selectedTag = key.mid(QStringLiteral("tag:").size()).toLower();
    }

    const bool categoryView = (selectedFolder == QStringLiteral("inbox")
                               && !selectedCategories.isEmpty()
                               && selectedCategoryIndex >= 0
                               && selectedCategoryIndex < selectedCategories.size());

    if (!selectedTag.isEmpty()) {
        QSet<QString> taggedMessageIds;
        
        if (selectedTag == QStringLiteral("important")) {
            // Server-synced important (e.g. [Gmail]/Important folder edge)
            QSqlQuery qTag(database);
            qTag.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_folder_map WHERE lower(folder) LIKE '%/important'"));
            if (qTag.exec()) {
                while (qTag.next()) taggedMessageIds.insert(qTag.value(0).toString());
            }
            // Also include locally-toggled important flag
            QSqlQuery qLocal(database);
            qLocal.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)='important'"));
            if (qLocal.exec()) {
                while (qLocal.next()) taggedMessageIds.insert(qLocal.value(0).toString());
            }
        } else {
            QSqlQuery qTag(database);
            qTag.prepare(QStringLiteral("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)=:label"));
            qTag.bindValue(QStringLiteral(":label"), selectedTag);
            if (qTag.exec()) {
                while (qTag.next()) taggedMessageIds.insert(qTag.value(0).toString());
            }
        }

        QVariantList filtered;
        QSet<QString> seen;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            const QString mid = r.value(QStringLiteral("messageId")).toString();
            if (mid.isEmpty() || !taggedMessageIds.contains(mid)) continue;
            if (trashedMessageIds.contains(mid)) continue;
            if (seen.contains(mid)) continue;
            seen.insert(mid);
            filtered.push_back(v);
        }
        return pageRows(filtered);
    }

    if (!selectedFolder.isEmpty() && !categoryView) {
        const bool selectedIsTrash = isTrashFolderName(selectedFolder);

        // Folder views must come from DB (not m_inbox window), otherwise large folders
        // like Trash/All Mail show only a handful of rows.
        QVariantList filtered;
        if (database.isValid() && database.isOpen()) {
            QSqlQuery qFolder(database);
            if (selectedIsTrash) {
                qFolder.prepare(QStringLiteral(R"(
                    SELECT mfm.account_email,
                           mfm.folder,
                           mfm.uid,
                           cm.id,
                           cm.sender,
                           cm.subject,
                           cm.recipient,
                           '' as recipient_avatar_url,
                           cm.received_at,
                           cm.snippet,
                           COALESCE(cm.has_tracking_pixel, 0) as has_tracking_pixel,
                           '' as avatar_domain,
                           '' as avatar_url,
                           '' as avatar_source,
                           mfm.unread,
                           COALESCE(cm.thread_id, '') as thread_id,
                           COALESCE((SELECT COUNT(DISTINCT m2.id) FROM messages m2
                                     WHERE m2.account_email = cm.account_email
                                       AND m2.thread_id = cm.thread_id
                                       AND cm.thread_id IS NOT NULL
                                       AND length(trim(COALESCE(cm.thread_id, ''))) > 0), 1) as thread_count,
                           COALESCE(cm.gm_thr_id, '') as gm_thr_id
                    FROM message_folder_map mfm
                    JOIN messages cm ON cm.id = mfm.message_id
                    WHERE lower(mfm.folder)=:folder
                    ORDER BY cm.received_at DESC
                    LIMIT :limit OFFSET :offset
                )"));
                qFolder.bindValue(QStringLiteral(":folder"), selectedFolder);
                qFolder.bindValue(QStringLiteral(":limit"), (limit > 0 ? (limit + 1) : 5000));
                qFolder.bindValue(QStringLiteral(":offset"), qMax(0, offset));
            } else {
                qFolder.prepare(QStringLiteral(R"(
                    SELECT mfm.account_email,
                           mfm.folder,
                           mfm.uid,
                           cm.id,
                           cm.sender,
                           cm.subject,
                           cm.recipient,
                           '' as recipient_avatar_url,
                           cm.received_at,
                           cm.snippet,
                           COALESCE(cm.has_tracking_pixel, 0) as has_tracking_pixel,
                           '' as avatar_domain,
                           '' as avatar_url,
                           '' as avatar_source,
                           mfm.unread,
                           COALESCE(cm.thread_id, '') as thread_id,
                           COALESCE((SELECT COUNT(DISTINCT m2.id) FROM messages m2
                                     WHERE m2.account_email = cm.account_email
                                       AND m2.thread_id = cm.thread_id
                                       AND cm.thread_id IS NOT NULL
                                       AND length(trim(COALESCE(cm.thread_id, ''))) > 0), 1) as thread_count,
                           COALESCE(cm.gm_thr_id, '') as gm_thr_id
                    FROM message_folder_map mfm
                    JOIN messages cm ON cm.id = mfm.message_id
                    WHERE lower(mfm.folder)=:folder
                      AND NOT EXISTS (
                          SELECT 1 FROM message_folder_map t
                          WHERE t.account_email=mfm.account_email
                            AND t.message_id=mfm.message_id
                            AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
                      )
                    ORDER BY cm.received_at DESC
                    LIMIT :limit OFFSET :offset
                )"));
                qFolder.bindValue(QStringLiteral(":folder"), selectedFolder);
                qFolder.bindValue(QStringLiteral(":limit"), (limit > 0 ? (limit + 1) : 5000));
                qFolder.bindValue(QStringLiteral(":offset"), qMax(0, offset));
            }

            if (qFolder.exec()) {
                QSet<QString> seenFolderTids;
                while (qFolder.next()) {
                    const QString tid   = qFolder.value(15).toString().trimmed();
                    QString gtid        = qFolder.value(17).toString().trimmed();
                    const QString acct  = qFolder.value(0).toString();
                    const QString mid   = qFolder.value(3).toString();
                    if (gtid.isEmpty() && tid.startsWith(QStringLiteral("gm:"), Qt::CaseInsensitive))
                        gtid = tid.mid(3).trimmed();
                    const QString tkey  = !gtid.isEmpty()
                        ? acct + QStringLiteral("|gtid:") + gtid
                        : (tid.isEmpty()
                            ? acct + QStringLiteral("|msg:") + mid
                            : acct + QStringLiteral("|tid:") + tid);
                    if (seenFolderTids.contains(tkey)) continue;
                    seenFolderTids.insert(tkey);

                    QVariantMap row;
                    row.insert(QStringLiteral("accountEmail"), qFolder.value(0));
                    row.insert(QStringLiteral("folder"), qFolder.value(1));
                    row.insert(QStringLiteral("uid"), qFolder.value(2));
                    row.insert(QStringLiteral("messageId"), mid);
                    row.insert(QStringLiteral("sender"), qFolder.value(4));
                    row.insert(QStringLiteral("subject"), qFolder.value(5));
                    row.insert(QStringLiteral("recipient"), qFolder.value(6));
                    row.insert(QStringLiteral("recipientAvatarUrl"), qFolder.value(7));
                    row.insert(QStringLiteral("receivedAt"), qFolder.value(8));
                    row.insert(QStringLiteral("snippet"),          qFolder.value(9));
                    row.insert(QStringLiteral("hasTrackingPixel"), qFolder.value(10).toInt() == 1);
                    row.insert(QStringLiteral("avatarDomain"),     qFolder.value(11));
                    row.insert(QStringLiteral("avatarUrl"), qFolder.value(12));
                    row.insert(QStringLiteral("avatarSource"), qFolder.value(13));
                    row.insert(QStringLiteral("unread"), qFolder.value(14).toInt() == 1);
                    row.insert(QStringLiteral("threadId"),    tid);
                    row.insert(QStringLiteral("threadCount"), qFolder.value(16).toInt());
                    row.insert(QStringLiteral("gmThrId"),     gtid);
                    filtered.push_back(row);
                }
            }
        }

        if (limit > 0 && filtered.size() > limit) {
            if (hasMore) *hasMore = true;
            filtered = filtered.mid(0, limit);
        }
        annotateMessageFlags(filtered);
        return filtered;
    }

    if (categoryView && database.isValid() && database.isOpen()) {
        const QString cat = selectedCategories.at(selectedCategoryIndex).toLower();

        auto trashExistsExpr = QStringLiteral(
            "EXISTS (SELECT 1 FROM message_folder_map t "
            "WHERE t.account_email=m.account_email AND t.message_id=m.message_id "
            "AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash'))");

        auto labelHas = [](const QString &needle) {
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM message_labels ml "
                "WHERE ml.account_email=m.account_email AND ml.message_id=m.message_id "
                "AND lower(ml.label) LIKE '%%1%')").arg(needle);
        };

        const QString anySmartLabel = QStringLiteral(
            "EXISTS (SELECT 1 FROM message_labels ml "
            "WHERE ml.account_email=m.account_email AND ml.message_id=m.message_id "
            "AND (lower(ml.label) LIKE '%/categories/primary%' OR lower(ml.label) LIKE '%/categories/promotion%' "
            "OR lower(ml.label) LIKE '%/categories/social%' OR lower(ml.label) LIKE '%/categories/update%' "
            "OR lower(ml.label) LIKE '%/categories/forum%' OR lower(ml.label) LIKE '%/categories/purchase%'))");
        const QString inboxMap = QStringLiteral(
            "EXISTS (SELECT 1 FROM message_folder_map mf2 "
            "WHERE mf2.account_email=m.account_email AND mf2.message_id=m.message_id "
            "AND (lower(mf2.folder)='inbox' OR lower(mf2.folder)='[gmail]/inbox' OR lower(mf2.folder)='[google mail]/inbox' OR lower(mf2.folder) LIKE '%/inbox'))");

        QString categoryMatch;
        QString preferredFolderLike = QStringLiteral("%/categories/primary%");
        if (cat.contains(QStringLiteral("promotion"))) {
            categoryMatch = labelHas(QStringLiteral("/categories/promotion"));
            preferredFolderLike = QStringLiteral("%/categories/promotion%");
        } else if (cat.contains(QStringLiteral("social"))) {
            categoryMatch = labelHas(QStringLiteral("/categories/social"));
            preferredFolderLike = QStringLiteral("%/categories/social%");
        } else if (cat.contains(QStringLiteral("purchase"))) {
            categoryMatch = labelHas(QStringLiteral("/categories/purchase"));
            preferredFolderLike = QStringLiteral("%/categories/purchase%");
        } else if (cat.contains(QStringLiteral("update"))) {
            categoryMatch = labelHas(QStringLiteral("/categories/update"));
            preferredFolderLike = QStringLiteral("%/categories/update%");
        } else if (cat.contains(QStringLiteral("forum"))) {
            categoryMatch = labelHas(QStringLiteral("/categories/forum"));
            preferredFolderLike = QStringLiteral("%/categories/forum%");
        } else {
            categoryMatch = QStringLiteral("(%1 OR (%2 AND NOT %3))")
                    .arg(labelHas(QStringLiteral("/categories/primary")),
                         inboxMap,
                         anySmartLabel);
            preferredFolderLike = QStringLiteral("%/categories/primary%");
        }

        const QString idsSql = QStringLiteral(R"(
            SELECT m.account_email, m.message_id
            FROM message_folder_map m
            JOIN messages cm ON cm.id = m.message_id AND cm.account_email = m.account_email
            WHERE NOT %1
              AND %2
            GROUP BY m.account_email, m.message_id
            ORDER BY cm.received_at DESC
            LIMIT :limit OFFSET :offset
        )").arg(trashExistsExpr, categoryMatch);

        QSqlQuery qIds(database);
        qIds.prepare(idsSql);
        qIds.bindValue(QStringLiteral(":limit"), (limit > 0 ? (limit + 1) : 5001));
        qIds.bindValue(QStringLiteral(":offset"), qMax(0, offset));

        QVariantList out;
        if (qIds.exec()) {
            struct Key { QString account; qint64 messageId; };
            QVector<Key> keys;
            while (qIds.next()) {
                keys.push_back({qIds.value(0).toString(), qIds.value(1).toLongLong()});
            }
            if (limit > 0 && keys.size() > limit) {
                if (hasMore) *hasMore = true;
                keys.resize(limit);
            }

            QSqlQuery qRow(database);
            qRow.prepare(QStringLiteral(R"(
                SELECT mfm.account_email,
                       mfm.folder,
                       mfm.uid,
                       cm.id,
                       cm.sender,
                       cm.subject,
                       cm.recipient,
                       '' as recipient_avatar_url,
                       cm.received_at,
                       cm.snippet,
                       COALESCE(cm.has_tracking_pixel, 0) as has_tracking_pixel,
                       '' as avatar_domain,
                       '' as avatar_url,
                       '' as avatar_source,
                       mfm.unread,
                       COALESCE(cm.thread_id, '') as thread_id,
                       COALESCE((SELECT COUNT(DISTINCT m2.id) FROM messages m2
                                 WHERE m2.account_email = cm.account_email
                                   AND m2.thread_id = cm.thread_id
                                   AND cm.thread_id IS NOT NULL
                                   AND length(trim(COALESCE(cm.thread_id, ''))) > 0), 1) as thread_count,
                       COALESCE(cm.gm_thr_id, '') as gm_thr_id
                FROM message_folder_map mfm
                JOIN messages cm ON cm.id = mfm.message_id
                WHERE mfm.account_email=:account_email
                  AND mfm.message_id=:message_id
                ORDER BY CASE
                    WHEN lower(mfm.folder) LIKE :preferred THEN 0
                    WHEN lower(mfm.folder)='inbox' OR lower(mfm.folder)='[gmail]/inbox' OR lower(mfm.folder)='[google mail]/inbox' OR lower(mfm.folder) LIKE '%/inbox' THEN 1
                    ELSE 2
                END,
                mfm.rowid DESC
                LIMIT 1
            )"));

            QSet<QString> seenCatTids;
            for (const Key &k : keys) {
                qRow.bindValue(QStringLiteral(":account_email"), k.account);
                qRow.bindValue(QStringLiteral(":message_id"), k.messageId);
                qRow.bindValue(QStringLiteral(":preferred"), preferredFolderLike);
                if (!qRow.exec() || !qRow.next()) continue;

                const QString tid  = qRow.value(15).toString().trimmed();
                QString gtid       = qRow.value(17).toString().trimmed();
                const QString acct = qRow.value(0).toString();
                const QString mid  = qRow.value(3).toString();
                if (gtid.isEmpty() && tid.startsWith(QStringLiteral("gm:"), Qt::CaseInsensitive))
                    gtid = tid.mid(3).trimmed();
                const QString tkey = !gtid.isEmpty()
                    ? acct + QStringLiteral("|gtid:") + gtid
                    : (tid.isEmpty()
                        ? acct + QStringLiteral("|msg:") + mid
                        : acct + QStringLiteral("|tid:") + tid);
                if (seenCatTids.contains(tkey)) continue;
                seenCatTids.insert(tkey);

                QVariantMap row;
                row.insert(QStringLiteral("accountEmail"), qRow.value(0));
                row.insert(QStringLiteral("folder"), qRow.value(1));
                row.insert(QStringLiteral("uid"), qRow.value(2));
                row.insert(QStringLiteral("messageId"), mid);
                row.insert(QStringLiteral("sender"), qRow.value(4));
                row.insert(QStringLiteral("subject"), qRow.value(5));
                row.insert(QStringLiteral("recipient"), qRow.value(6));
                row.insert(QStringLiteral("recipientAvatarUrl"), qRow.value(7));
                row.insert(QStringLiteral("receivedAt"), qRow.value(8));
                row.insert(QStringLiteral("snippet"),          qRow.value(9));
                row.insert(QStringLiteral("hasTrackingPixel"), qRow.value(10).toInt() == 1);
                row.insert(QStringLiteral("avatarDomain"),     qRow.value(11));
                row.insert(QStringLiteral("avatarUrl"), qRow.value(12));
                row.insert(QStringLiteral("avatarSource"), qRow.value(13));
                row.insert(QStringLiteral("unread"), qRow.value(14).toInt() == 1);
                row.insert(QStringLiteral("threadId"),    tid);
                row.insert(QStringLiteral("threadCount"), qRow.value(16).toInt());
                row.insert(QStringLiteral("gmThrId"),     gtid);
                out.push_back(row);
            }
        }

        annotateMessageFlags(out);
        return out;
    }

    return pageRows(rows);
}

QVariantList DataStore::groupedMessagesForSelection(const QString &folderKey,
                                                    const QStringList &selectedCategories,
                                                    int selectedCategoryIndex,
                                                    bool todayExpanded,
                                                    bool yesterdayExpanded,
                                                    bool lastWeekExpanded,
                                                    bool twoWeeksAgoExpanded,
                                                    bool olderExpanded) const
{
    const QVariantList rows = messagesForSelection(folderKey, selectedCategories, selectedCategoryIndex);

    auto bucketKeyForDate = [](const QString &dateValue) -> QString {
        const QDateTime dt = QDateTime::fromString(dateValue, Qt::ISODate);
        if (!dt.isValid()) return QStringLiteral("older");
        const QDate target = dt.toLocalTime().date();
        const QDate today = QDate::currentDate();
        const int diffDays = target.daysTo(today);
        if (diffDays <= 0) return QStringLiteral("today");
        if (diffDays == 1) return QStringLiteral("yesterday");

        const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7));
        if (target >= weekStart && target < today) {
            return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
        }

        if (diffDays <= 14) return QStringLiteral("lastWeek");
        if (diffDays <= 21) return QStringLiteral("twoWeeksAgo");
        return QStringLiteral("older");
    };

    auto bucketLabel = [](const QString &bucketKey) -> QString {
        if (bucketKey == QStringLiteral("today")) return QStringLiteral("Today");
        if (bucketKey == QStringLiteral("yesterday")) return QStringLiteral("Yesterday");
        if (bucketKey.startsWith(QStringLiteral("weekday-"))) {
            bool ok = false;
            const int dow = bucketKey.mid(QStringLiteral("weekday-").size()).toInt(&ok);
            if (ok && dow >= 1 && dow <= 7) return QLocale().dayName(dow, QLocale::LongFormat);
        }
        if (bucketKey == QStringLiteral("lastWeek")) return QStringLiteral("Last Week");
        if (bucketKey == QStringLiteral("twoWeeksAgo")) return QStringLiteral("Two Weeks Ago");
        return QStringLiteral("Older");
    };

    auto isExpanded = [&](const QString &bucketKey) {
        if (bucketKey == QStringLiteral("today")) return todayExpanded;
        if (bucketKey == QStringLiteral("yesterday")) return yesterdayExpanded;
        if (bucketKey.startsWith(QStringLiteral("weekday-"))) return true;
        if (bucketKey == QStringLiteral("lastWeek")) return lastWeekExpanded;
        if (bucketKey == QStringLiteral("twoWeeksAgo")) return twoWeeksAgoExpanded;
        return olderExpanded;
    };

    QHash<QString, QVariantList> buckets;
    buckets.insert(QStringLiteral("today"), {});
    buckets.insert(QStringLiteral("yesterday"), {});
    buckets.insert(QStringLiteral("lastWeek"), {});
    buckets.insert(QStringLiteral("twoWeeksAgo"), {});
    buckets.insert(QStringLiteral("older"), {});

    const QDate today = QDate::currentDate();
    const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7));
    QStringList weekdayOrder;
    for (QDate d = today.addDays(-2); d >= weekStart; d = d.addDays(-1)) {
        const QString key = QStringLiteral("weekday-%1").arg(d.dayOfWeek());
        if (!weekdayOrder.contains(key)) {
            weekdayOrder.push_back(key);
            buckets.insert(key, {});
        }
    }

    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        const QString key = bucketKeyForDate(row.value(QStringLiteral("receivedAt")).toString());
        auto list = buckets.value(key);
        list.push_back(row);
        buckets.insert(key, list);
    }

    QStringList order;
    order << QStringLiteral("today") << QStringLiteral("yesterday");
    order << weekdayOrder;
    order << QStringLiteral("lastWeek") << QStringLiteral("twoWeeksAgo") << QStringLiteral("older");
    QVariantList out;
    for (const QString &key : order) {
        const QVariantList rowsInBucket = buckets.value(key);
        if (rowsInBucket.isEmpty()) continue;

        QVariantMap header;
        header.insert(QStringLiteral("kind"), QStringLiteral("header"));
        header.insert(QStringLiteral("bucketKey"), key);
        header.insert(QStringLiteral("title"), bucketLabel(key));
        header.insert(QStringLiteral("expanded"), isExpanded(key));
        header.insert(QStringLiteral("hasTopGap"), !out.isEmpty());
        out.push_back(header);

        if (isExpanded(key)) {
            for (const QVariant &rv : rowsInBucket) {
                QVariantMap msg;
                msg.insert(QStringLiteral("kind"), QStringLiteral("message"));
                msg.insert(QStringLiteral("row"), rv.toMap());
                out.push_back(msg);
            }
        }
    }

    return out;
}

QString DataStore::avatarDirPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + "/kestrel-mail/avatars"_L1;
}

QString DataStore::writeAvatarDataUri(const QString &email, const QString &dataUri)
{
    // Parse "data:image/TYPE;base64,BASE64DATA"
    static const QRegularExpression re(QStringLiteral(R"(^data:(image/[^;,]+);base64,(.+)$)"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(dataUri.trimmed());
    if (!m.hasMatch())
        return {};

    const QString mime = m.captured(1).trimmed().toLower();
    const QByteArray bytes = QByteArray::fromBase64(m.captured(2).trimmed().toLatin1());
    if (bytes.isEmpty())
        return {};

    QString ext = "bin"_L1;
    if (mime.startsWith("image/png"_L1))        ext = "png"_L1;
    else if (mime.contains("jpeg"_L1) || mime.contains("jpg"_L1)) ext = "jpg"_L1;
    else if (mime.startsWith("image/webp"_L1))  ext = "webp"_L1;
    else if (mime.startsWith("image/gif"_L1))   ext = "gif"_L1;
    else if (mime.startsWith("image/svg"_L1))   ext = "svg"_L1;

    const QString dir = avatarDirPath();
    QDir().mkpath(dir);

    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(email.trimmed().toLower().toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString absPath = dir + "/"_L1 + hash + "."_L1 + ext;

    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    f.close();

    return "file://"_L1 + absPath;
}

QString DataStore::avatarForEmail(const QString &email) const
{
    // Off-thread callers get no result — avatars are only needed on the main thread.
    if (QThread::currentThread() != thread())
        return {};

    const QString e = email.trimmed().toLower();
    if (e.isEmpty())
        return {};

    // Fast path: in-memory cache populated on first DB hit.
    if (m_avatarCache.contains(e))
        return m_avatarCache.value(e);

    auto database = db();
    if (database.isValid() && database.isOpen()) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("SELECT avatar_url FROM contact_avatars WHERE email=:email LIMIT 1"));
        q.bindValue(QStringLiteral(":email"), e);
        if (q.exec() && q.next()) {
            const QString cached = q.value(0).toString().trimmed();
            m_avatarCache.insert(e, cached);
            return cached;
        }
    }

    m_avatarCache.insert(e, {});
    return {};
}

QString DataStore::displayNameForEmail(const QString &email) const
{
    if (QThread::currentThread() != thread()) {
        return {};
    }

    const QString e = email.trimmed().toLower();
    if (e.isEmpty()) return {};

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT display_name FROM contact_display_names WHERE email=:email LIMIT 1"));
    q.bindValue(QStringLiteral(":email"), e);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString().trimmed();
}

QVariantList DataStore::searchContacts(const QString &prefix, int limit) const
{
    QVariantList out;
    if (QThread::currentThread() != thread())
        return out;

    const QString p = prefix.trimmed();
    if (p.isEmpty())
        return out;

    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return out;

    const QString pattern = p + QLatin1Char('%');
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT email, display_name FROM contact_display_names "
        "WHERE email LIKE :pat OR display_name LIKE :pat2 "
        "ORDER BY display_score DESC, last_seen_at DESC "
        "LIMIT :lim"
    ));
    q.bindValue(QStringLiteral(":pat"),  pattern);
    q.bindValue(QStringLiteral(":pat2"), pattern);
    q.bindValue(QStringLiteral(":lim"),  limit);
    if (!q.exec())
        return out;

    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("email"),       q.value(0).toString());
        row.insert(QStringLiteral("displayName"), q.value(1).toString().trimmed());
        out.append(row);
    }
    return out;
}

QVariantMap DataStore::migrationStats() const
{
    QVariantMap out;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return out;

    auto scalar = [&](const QString &sql) -> int {
        QSqlQuery q(database);
        if (!q.exec(sql) || !q.next()) return -1;
        return q.value(0).toInt();
    };

    out.insert(QStringLiteral("messages"), scalar(QStringLiteral("SELECT count(*) FROM messages")));
    out.insert(QStringLiteral("folderEdges"), scalar(QStringLiteral("SELECT count(*) FROM message_folder_map")));
    out.insert(QStringLiteral("labels"), scalar(QStringLiteral("SELECT count(*) FROM message_labels")));
    out.insert(QStringLiteral("tags"), scalar(QStringLiteral("SELECT count(*) FROM tags")));
    out.insert(QStringLiteral("tagMaps"), scalar(QStringLiteral("SELECT count(*) FROM message_tag_map")));

    out.insert(QStringLiteral("labelsMissingEdge"), scalar(QStringLiteral(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )")));

    out.insert(QStringLiteral("edgesMissingLabel"), scalar(QStringLiteral(R"(
        SELECT count(*)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
    )")));

    out.insert(QStringLiteral("labelsMissingTagMap"), scalar(QStringLiteral(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE NOT EXISTS (
            SELECT 1
            FROM tags t
            JOIN message_tag_map mtm ON mtm.tag_id = t.id
            WHERE t.normalized_name = lower(ml.label)
              AND mtm.account_email = ml.account_email
              AND mtm.message_id = ml.message_id
        )
    )")));

    return out;
}

QString DataStore::preferredSelfDisplayName(const QString &accountEmail) const
{
    if (QThread::currentThread() != thread()) return {};

    const QString e = accountEmail.trimmed().toLower();
    if (e.isEmpty()) return {};

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    QHash<QString, int> scores;
    auto consider = [&](const QString &raw, int weight) {
        const QString name = extractExplicitDisplayName(raw, e).trimmed();
        if (name.isEmpty()) return;
        if (name.compare(e, Qt::CaseInsensitive) == 0) return;
        int s = weight;
        if (name == name.toUpper() && name.size() > 3) s -= 2;
        if (name.contains(' ')) s += 2;
        scores[name] += s;
    };

    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT sender, recipient FROM messages WHERE lower(sender) LIKE :pat OR lower(recipient) LIKE :pat"));
    q.bindValue(QStringLiteral(":pat"), QStringLiteral("%<") + e + QStringLiteral(">%"));
    if (q.exec()) {
        while (q.next()) {
            consider(q.value(0).toString(), 3); // sender name is usually authoritative for self identity
            consider(q.value(1).toString(), 1);
        }
    }

    QString best;
    int bestScore = std::numeric_limits<int>::min();
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        if (it.value() > bestScore) {
            bestScore = it.value();
            best = it.key();
        }
    }
    return best.trimmed();
}

bool DataStore::avatarShouldRefresh(const QString &email, int ttlSeconds, int maxFailures) const
{
    if (QThread::currentThread() != thread()) {
        // DataStore DB connection is thread-affine; off-thread callers should not query it directly.
        return true;
    }

    const QString e = email.trimmed().toLower();
    if (e.isEmpty()) return false;

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return true;

    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT avatar_url, last_checked_at, failure_count FROM contact_avatars WHERE email=:email LIMIT 1"));
    q.bindValue(QStringLiteral(":email"), e);
    if (!q.exec() || !q.next()) return true;

    const QString avatarUrl = q.value(0).toString().trimmed();
    const QString checked = q.value(1).toString().trimmed();
    const int failures = q.value(2).toInt();

    // File URLs are fully resolved — no need to refresh unless the file was deleted.
    if (avatarUrl.startsWith("file://"_L1))
        return false;
    // Raw HTTPS URLs were likely stored before this refactor (they are now cleared on startup)
    // or when a fetch failed. Allow retry so we can eventually store a proper file URL.

    const QDateTime checkedAt = QDateTime::fromString(checked, Qt::ISODate);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 age = checkedAt.isValid() ? checkedAt.secsTo(now) : (ttlSeconds + 1);

    if (failures >= maxFailures) {
        const int backoff = ttlSeconds * qMin(failures, 8);
        return age >= backoff;
    }
    return age >= ttlSeconds;
}

QStringList DataStore::staleGooglePeopleEmails(int limit) const
{
    if (QThread::currentThread() != thread()) return {};
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT email FROM contact_avatars
        WHERE source='google-people'
          AND (length(trim(avatar_url)) = 0
               OR avatar_url LIKE 'https://%'
               OR avatar_url LIKE 'http://%')
          AND (last_checked_at IS NULL
               OR datetime(last_checked_at) < datetime('now', '-1 hour'))
        ORDER BY last_checked_at ASC
        LIMIT :lim
    )"));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec()) return {};

    QStringList result;
    while (q.next())
        result << q.value(0).toString().trimmed().toLower();
    return result;
}

void DataStore::updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString e = email.trimmed().toLower();
    if (e.isEmpty()) return;

    const QString storedUrl = avatarUrl.trimmed();

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
        VALUES (:email, :avatar_url, :source, datetime('now'), 0)
        ON CONFLICT(email) DO UPDATE SET
          avatar_url=excluded.avatar_url,
          source=excluded.source,
          last_checked_at=datetime('now'),
          failure_count=0
    )"));
    q.bindValue(QStringLiteral(":email"), e);
    q.bindValue(QStringLiteral(":avatar_url"), storedUrl);
    q.bindValue(QStringLiteral(":source"), source.trimmed().isEmpty()
                ? QStringLiteral("google-people") : source.trimmed().toLower());
    q.exec();
    m_avatarCache.insert(e, storedUrl);
}

bool DataStore::hasCachedHeadersForFolder(const QString &rawFolderName, int minCount) const
{
    const QString folder = rawFolderName.trimmed().toLower();
    if (folder.isEmpty()) return false;

    int count = 0;
    for (const QVariant &v : m_inbox) {
        if (v.toMap().value(QStringLiteral("folder")).toString().toLower() == folder) {
            ++count;
            if (count >= minCount) return true;
        }
    }
    return false;
}

QVariantMap DataStore::statsForFolder(const QString &folderKey, const QString &rawFolderName) const
{
    QVariantMap out;
    const QVariantList rows = m_inbox;
    int total = 0;
    int unread = 0;

    const QString key = folderKey.trimmed().toLower();
    if (key == QStringLiteral("favorites:all-inboxes")) {
        total = rows.size();
        for (const QVariant &v : rows) if (v.toMap().value(QStringLiteral("unread")).toBool()) ++unread;
    } else if (key == QStringLiteral("favorites:unread")) {
        for (const QVariant &v : rows) if (v.toMap().value(QStringLiteral("unread")).toBool()) ++unread;
        total = unread;
    } else if (key == QStringLiteral("favorites:flagged") || key.startsWith(QStringLiteral("local:"))) {
        total = 0;
        unread = 0;
    } else if (key.startsWith(QStringLiteral("tag:"))) {
        const QString tag = key.mid(QStringLiteral("tag:").size());
        auto database = db();
        if (database.isValid() && database.isOpen() && !tag.isEmpty()) {
            QSqlQuery q(database);
            q.prepare(QStringLiteral(R"(
                SELECT COUNT(DISTINCT ml.message_id),
                       SUM(CASE WHEN EXISTS (
                            SELECT 1 FROM message_folder_map mfm
                            WHERE mfm.account_email=ml.account_email
                              AND mfm.message_id=ml.message_id
                              AND mfm.unread=1
                       ) THEN 1 ELSE 0 END)
                FROM message_labels ml
                WHERE lower(ml.label)=:label
            )"));
            q.bindValue(QStringLiteral(":label"), tag);
            if (q.exec() && q.next()) {
                total = q.value(0).toInt();
                unread = q.value(1).toInt();
            }
        }
    } else {
        QString folder = rawFolderName.trimmed().toLower();
        if (folder.isEmpty() && key.startsWith(QStringLiteral("account:"))) {
            folder = key.mid(QStringLiteral("account:").size());
        }

        auto database = db();
        if (database.isValid() && database.isOpen() && !folder.isEmpty()) {
            if (folder == QStringLiteral("inbox")) {
                // Accurate Inbox count = category folders + messages only in INBOX.
                QSqlQuery q(database);
                q.prepare(QStringLiteral(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN EXISTS (
                                SELECT 1 FROM message_folder_map x
                                WHERE x.account_email = mfm.account_email
                                  AND x.message_id = mfm.message_id
                                  AND x.unread = 1
                           ) THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder) IN (
                        'inbox',
                        '[gmail]/categories/primary',
                        '[gmail]/categories/promotions',
                        '[gmail]/categories/social',
                        '[gmail]/categories/updates',
                        '[gmail]/categories/forums',
                        '[gmail]/categories/purchases',
                        '[google mail]/categories/primary',
                        '[google mail]/categories/promotions',
                        '[google mail]/categories/social',
                        '[google mail]/categories/updates',
                        '[google mail]/categories/forums',
                        '[google mail]/categories/purchases'
                    )
                )"));
                if (q.exec() && q.next()) {
                    total = q.value(0).toInt();
                    unread = q.value(1).toInt();
                }
            } else {
                QSqlQuery q(database);
                q.prepare(QStringLiteral(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN mfm.unread=1 THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder)=:folder
                )"));
                q.bindValue(QStringLiteral(":folder"), folder);
                if (q.exec() && q.next()) {
                    total = q.value(0).toInt();
                    unread = q.value(1).toInt();
                }
            }
        } else {
            for (const QVariant &v : rows) {
                const QVariantMap r = v.toMap();
                if (r.value(QStringLiteral("folder")).toString().toLower() != folder) continue;
                ++total;
                if (r.value(QStringLiteral("unread")).toBool()) ++unread;
            }
        }
    }

    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("unread"), unread);
    return out;
}

void DataStore::reloadFolders()
{
    m_folders.clear();
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        emit foldersChanged();
        return;
    }

    QSqlQuery q(database);
    q.exec(QStringLiteral(R"(
      SELECT account_email, name, flags, special_use
      FROM folders
      ORDER BY
        CASE
          WHEN UPPER(name)='INBOX' THEN 0
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/PRIMARY' THEN 1
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/PROMOTIONS' THEN 2
          WHEN UPPER(name)='[GMAIL]/CATEGORIES/SOCIAL' THEN 3
          ELSE 10
        END,
        name COLLATE NOCASE
    )"));

    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("accountEmail"), q.value(0));
        row.insert(QStringLiteral("name"), q.value(1));
        row.insert(QStringLiteral("flags"), q.value(2));
        row.insert(QStringLiteral("specialUse"), q.value(3));
        m_folders.push_back(row);
    }

    emit foldersChanged();
}

void DataStore::upsertAttachments(qint64 messageId, const QString &accountEmail, const QVariantList &attachments)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;

    for (const QVariant &v : attachments) {
        const QVariantMap a = v.toMap();
        const QString partId   = a.value(QStringLiteral("partId")).toString().trimmed();
        const QString name     = a.value(QStringLiteral("name")).toString().trimmed();
        const QString mimeType = a.value(QStringLiteral("mimeType")).toString().trimmed();
        const int encodedBytes = a.value(QStringLiteral("encodedBytes")).toInt();
        const QString encoding = a.value(QStringLiteral("encoding")).toString().trimmed();

        if (partId.isEmpty()) continue;

        QSqlQuery q(database);
        q.prepare(QStringLiteral(R"(
            INSERT INTO message_attachments (message_id, account_email, part_id, name, mime_type, encoded_bytes, encoding)
            VALUES (:message_id, :account_email, :part_id, :name, :mime_type, :encoded_bytes, :encoding)
            ON CONFLICT(account_email, message_id, part_id) DO UPDATE SET
              name=excluded.name,
              mime_type=excluded.mime_type,
              encoded_bytes=excluded.encoded_bytes,
              encoding=excluded.encoding
        )"));
        q.bindValue(QStringLiteral(":message_id"),    messageId);
        q.bindValue(QStringLiteral(":account_email"), accountEmail);
        q.bindValue(QStringLiteral(":part_id"),       partId);
        q.bindValue(QStringLiteral(":name"),          name.isEmpty() ? QStringLiteral("Attachment") : name);
        q.bindValue(QStringLiteral(":mime_type"),     mimeType);
        q.bindValue(QStringLiteral(":encoded_bytes"), encodedBytes);
        q.bindValue(QStringLiteral(":encoding"),      encoding);
        q.exec();
    }
}

QVariantList DataStore::attachmentsForMessage(const QString &accountEmail, const QString &folder, const QString &uid) const
{
    QVariantList out;
    const auto database = db();
    if (!database.isValid() || !database.isOpen()) return out;

    // Resolve message_id via the folder edge, then fetch all attachment rows.
    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT DISTINCT ma.part_id, ma.name, ma.mime_type, ma.encoded_bytes, ma.encoding
        FROM message_attachments ma
        JOIN message_folder_map mfm ON mfm.message_id = ma.message_id
                                   AND mfm.account_email = ma.account_email
        WHERE ma.account_email = :account_email
          AND lower(mfm.folder) = lower(:folder)
          AND mfm.uid = :uid
        ORDER BY ma.part_id
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"),        folder.trimmed());
    q.bindValue(QStringLiteral(":uid"),           uid.trimmed());

    if (!q.exec()) return out;

    while (q.next()) {
        const QString encoding = q.value(4).toString();
        // Report decoded size: base64 encodes 3 bytes as 4 chars → multiply by 3/4.
        const int encodedBytes = q.value(3).toInt();
        const int displayBytes = (encoding.compare(QStringLiteral("base64"), Qt::CaseInsensitive) == 0)
                                 ? static_cast<int>(encodedBytes * 3 / 4)
                                 : encodedBytes;

        QVariantMap row;
        row.insert(QStringLiteral("partId"),   q.value(0).toString());
        row.insert(QStringLiteral("name"),     q.value(1).toString());
        row.insert(QStringLiteral("mimeType"), q.value(2).toString());
        row.insert(QStringLiteral("bytes"),    displayBytes);
        row.insert(QStringLiteral("encoding"), encoding);
        out.push_back(row);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Favorites config
// ---------------------------------------------------------------------------

QVariantList DataStore::favoritesConfig() const
{
    struct FavDef { QString key; QString name; };
    static const QList<FavDef> kDefs = {
        {"all-inboxes"_L1, "All Inboxes"_L1},
        {"unread"_L1,      "Unread"_L1     },
        {"flagged"_L1,     "Flagged"_L1    },
        {"outbox"_L1,      "Outbox"_L1     },
        {"sent"_L1,        "Sent"_L1       },
        {"trash"_L1,       "Trash"_L1      },
        {"drafts"_L1,      "Drafts"_L1     },
        {"junk"_L1,        "Junk Email"_L1 },
        {"archive"_L1,     "Archive"_L1    },
        {"unreplied"_L1,   "Unreplied"_L1  },
        {"snoozed"_L1,     "Snoozed"_L1   },
    };
    static const QSet<QString> kDefaultEnabled{"all-inboxes"_L1, "unread"_L1, "flagged"_L1};

    QSqlQuery q(db());
    q.exec("SELECT key, enabled FROM favorites_config"_L1);
    QHash<QString, bool> enabledMap;
    while (q.next())
        enabledMap.insert(q.value(0).toString(), q.value(1).toBool());

    QVariantList out;
    for (const auto &def : kDefs) {
        const bool en = enabledMap.contains(def.key)
                        ? enabledMap[def.key]
                        : kDefaultEnabled.contains(def.key);
        QVariantMap m;
        m["key"_L1]     = def.key;
        m["name"_L1]    = def.name;
        m["enabled"_L1] = en;
        out << m;
    }
    return out;
}

void DataStore::setFavoriteEnabled(const QString &key, bool enabled)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO favorites_config(key,enabled) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET enabled=excluded.enabled"_L1);
    q.addBindValue(key);
    q.addBindValue(enabled ? 1 : 0);
    q.exec();
    emit favoritesConfigChanged();
}

// ---------------------------------------------------------------------------
// User-created local folders
// ---------------------------------------------------------------------------

QVariantList DataStore::userFolders() const
{
    QSqlQuery q(db());
    q.exec("SELECT name FROM user_folders ORDER BY sort_order ASC, name ASC"_L1);
    QVariantList out;
    while (q.next()) {
        QVariantMap m;
        m["name"_L1] = q.value(0).toString();
        out << m;
    }
    return out;
}

bool DataStore::createUserFolder(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    QSqlQuery q(db());
    q.prepare("INSERT OR IGNORE INTO user_folders (name) VALUES (?)"_L1);
    q.addBindValue(n);
    if (!q.exec() || q.numRowsAffected() < 1) return false;
    emit userFoldersChanged();
    return true;
}

bool DataStore::deleteUserFolder(const QString &name)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM user_folders WHERE name = ?"_L1);
    q.addBindValue(name.trimmed());
    if (!q.exec() || q.numRowsAffected() < 1) return false;
    emit userFoldersChanged();
    return true;
}
