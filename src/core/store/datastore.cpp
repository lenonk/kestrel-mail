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
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

using namespace Qt::Literals::StringLiterals;

namespace {
static const QRegularExpression kReWhitespace(QStringLiteral("\\s+"));
static const QRegularExpression kReNonAlnumSplit(QStringLiteral("[^a-z0-9]+"));
static const QRegularExpression kReHasLetters(QStringLiteral("[A-Za-z]"));
static const QRegularExpression kReCsvSemicolonOutsideQuotes(QStringLiteral("[,;](?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)"));
static const QRegularExpression kReEmailAddress(QStringLiteral("([A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})"),
                                                QRegularExpression::CaseInsensitiveOption);

static const QRegularExpression kReSnippetUrl(QStringLiteral("https?://\\S+"),
                                              QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kReSnippetCharsetBoundary(QStringLiteral("\\b(?:charset|boundary)\\s*=\\s*\"?[^\"\\s]+\"?"),
                                                          QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kReSnippetViewEmailInBrowser(QStringLiteral("(?i)view\\s+(?:this\\s+)?email\\s+in\\s+(?:a|your)?\\s*browser[:!\\-\\s]*(?:https?://\\S+)?"));
static const QRegularExpression kReSnippetViewInBrowser(QStringLiteral("(?i)view\\s+in\\s+(?:a|your)?\\s*browser[:!\\-\\s]*(?:https?://\\S+)?"));
static const QRegularExpression kReSnippetViewAsWebPage(QStringLiteral("(?i)view\\s+as\\s+(?:a\\s+)?web\\s+page[:!\\-\\s]*(?:https?://\\S+)?"));
static const QRegularExpression kReSnippetTrailingPunct(QStringLiteral("\\s*[()\\[\\]{}|:;.,-]+\\s*$"));

static const QRegularExpression kReHtmlish(QStringLiteral("<html|<body|<div|<table|<p|<br|<span|<img|<a\\b"),
                                           QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression kReMarkdownLinks(QStringLiteral("\\[[^\\]\\n]{1,240}\\]\\(https?://[^\\s)]+\\)"),
                                                 QRegularExpression::CaseInsensitiveOption);
// Strip markdown link syntax [text](url) → replacement captures just the link text.
// Handles [text](url), [text]( ) and [text]( (partial — closing paren is optional).
static const QRegularExpression kReSnippetMarkdownLink(
    QStringLiteral(R"(\[([^\[\]\n]{1,160})\]\([^\)\n]{0,300}\)?)"));
// Runs of 4+ repeated separator characters, or space-separated patterns like - - - - -.
static const QRegularExpression kReSnippetSeparatorRun(
    QStringLiteral(R"([-=_*|#~<>]{4,}|(?:[-=_*|#~<>] ){3,}[-=_*|#~<>]?)"));
// Leftover empty or whitespace-only parentheses after URL/link stripping.
static const QRegularExpression kReSnippetEmptyParens(
    QStringLiteral(R"(\(\s*\))")
);
} // namespace

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
    const QStringList parts = val.trimmed().split(kReWhitespace, Qt::SkipEmptyParts);
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
    const auto m = kReEmailAddress.match(raw);
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

    const QStringList nameTokens = lower.split(kReNonAlnumSplit, Qt::SkipEmptyParts);
    if (nameTokens.size() >= 2) score += 4;

    if (!email.isEmpty()) {
        const int at = email.indexOf('@');
        const QString local = (at > 0) ? email.left(at) : email;
        const QStringList localTokens = local.split(kReNonAlnumSplit, Qt::SkipEmptyParts);
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

    const bool hasLetters = name.contains(kReHasLetters);
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
    const QStringList parts = s.split(kReCsvSemicolonOutsideQuotes, Qt::SkipEmptyParts);
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
            if (kReEmailAddress.match(candidate).hasMatch()) continue;

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

    if (kReEmailAddress.match(s).hasMatch()) return {};

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
    if (q1.exec() && q1.next()) {
        (void)q1.value(0).toInt();
    }

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
    if (q2.exec() && q2.next()) {
        (void)q2.value(0).toInt();
    }

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
    if (q1.exec() && q1.next()) {
        (void)q1.value(0).toInt();
    }

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
    if (q2.exec() && q2.next()) {
        (void)q2.value(0).toInt();
    }

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
    // JOIN against messages so orphan edges (message row deleted but edge remains)
    // are excluded — keeps the count consistent with what the sync engine actually has.
    q.prepare(QStringLiteral(R"(
        SELECT COUNT(*)
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email
        WHERE mfm.account_email=:account_email AND lower(mfm.folder)=:folder
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
    if (uids.isEmpty()) {
        // Wipe the entire folder.
        QSqlQuery q(database);
        q.prepare(QStringLiteral("DELETE FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"));
        q.bindValue(QStringLiteral(":account_email"), accountEmail);
        q.bindValue(QStringLiteral(":folder"), folder);
        q.exec();
        return q.numRowsAffected();
    }

    // Compute set-difference in C++ to avoid a huge NOT IN (...) clause.
    // A typical reconcile prune removes 0-10 UIDs from a mailbox of thousands,
    // so building the delete set in application memory is far cheaper than
    // sending a 30KB+ SQL string to SQLite and binding thousands of parameters.
    QSqlQuery qLocal(database);
    qLocal.prepare(QStringLiteral("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"));
    qLocal.bindValue(QStringLiteral(":account_email"), accountEmail);
    qLocal.bindValue(QStringLiteral(":folder"), folder);
    if (!qLocal.exec()) return 0;

    const QSet<QString> remoteSet(uids.begin(), uids.end());
    QStringList toDelete;
    while (qLocal.next()) {
        const QString uid = qLocal.value(0).toString().trimmed();
        if (!uid.isEmpty() && !remoteSet.contains(uid))
            toDelete.push_back(uid);
    }

    if (toDelete.isEmpty()) return 0;

    // Delete only the few stale UIDs (typically 0–10 even for large mailboxes).
    QStringList placeholders;
    placeholders.reserve(toDelete.size());
    for (int i = 0; i < toDelete.size(); ++i)
        placeholders << QStringLiteral(":d%1").arg(i);

    const QString sql = QStringLiteral(
        "DELETE FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder) AND uid IN (%1)")
        .arg(placeholders.join(QStringLiteral(",")));
    QSqlQuery qDel(database);
    qDel.prepare(sql);
    qDel.bindValue(QStringLiteral(":account_email"), accountEmail);
    qDel.bindValue(QStringLiteral(":folder"), folder);
    for (int i = 0; i < toDelete.size(); ++i)
        qDel.bindValue(QStringLiteral(":d%1").arg(i), toDelete.at(i));
    qDel.exec();
    return qDel.numRowsAffected();
}

}

DataStore::DataStore(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("kestrel_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

DataStore::~DataStore()
{
    QMutexLocker lock(&m_connMutex);
    for (const QString &connName : std::as_const(m_threadConnections)) {
        auto d = QSqlDatabase::database(connName, false);
        if (d.isValid()) d.close();
        QSqlDatabase::removeDatabase(connName);
    }
    m_threadConnections.clear();
}

QSqlDatabase DataStore::db() const
{
    const auto tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    {
        QMutexLocker lock(&m_connMutex);
        auto it = m_threadConnections.constFind(tid);
        if (it != m_threadConnections.constEnd())
            return QSqlDatabase::database(*it);
    }

    if (m_dbPath.isEmpty())
        return QSqlDatabase::database(m_connectionName);

    // Create a new per-thread connection for this worker thread.
    const QString connName = m_connectionName + "-t"_L1 + QString::number(tid, 16);
    {
        QSqlDatabase newDb = QSqlDatabase::addDatabase("QSQLITE"_L1, connName);
        newDb.setDatabaseName(m_dbPath);
        if (!newDb.open()) {
            qWarning() << "[DataStore] failed to open per-thread DB connection for thread" << tid;
            return newDb;
        }
        QSqlQuery q(newDb);
        q.exec("PRAGMA journal_mode=WAL"_L1);
        q.exec("PRAGMA busy_timeout=5000"_L1);
        q.exec("PRAGMA synchronous=NORMAL"_L1);
    }
    {
        QMutexLocker lock(&m_connMutex);
        m_threadConnections[tid] = connName;
    }
    return QSqlDatabase::database(connName);
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

    // Store path and register this thread's connection before any queries.
    m_dbPath = path;
    {
        QMutexLocker lock(&m_connMutex);
        m_threadConnections[reinterpret_cast<quintptr>(QThread::currentThreadId())] = m_connectionName;
    }

    QSqlQuery q(database);

    // Enable WAL mode so worker-thread per-connection reads don't block writes.
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

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
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN cc TEXT"));
    q.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN flagged INTEGER DEFAULT 0"));
    q.exec(QStringLiteral("ALTER TABLE folder_sync_status ADD COLUMN last_sync_modseq INTEGER NOT NULL DEFAULT 0"));

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
    // Clear snippets that contain residual markdown link artifacts, HTML entities,
    // or long separator runs — all signs that the extraction pipeline produced garbage.
    // These will be re-fetched and re-sanitized with the improved pipeline.
    // Note: GLOB is used for underscore/equals runs because LIKE treats '_' as a wildcard.
    q.exec(QStringLiteral(
        "UPDATE messages SET snippet = NULL "
        "WHERE snippet IS NOT NULL AND ("
        "  snippet LIKE '%[%](%' OR "
        "  snippet LIKE '%&ndash;%' OR snippet LIKE '%&mdash;%' OR "
        "  snippet LIKE '%&hellip;%' OR snippet LIKE '%&rsquo;%' OR "
        "  snippet GLOB '*=====*' OR snippet GLOB '*_____*'"
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
    // Standalone lower(folder) index for statsForFolder() WHERE lower(folder)=:f queries.
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_lower_folder ON message_folder_map(lower(folder))"));
    // Covering index for EXISTS(...unread=1) subqueries in statsForFolder().
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mfm_account_message_unread ON message_folder_map(account_email, message_id, unread)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ml_account_message_label ON message_labels(account_email, message_id, label)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ml_label_lower ON message_labels(lower(label))"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mtm_account_message_tag ON message_tag_map(account_email, message_id, tag_id)"));
    // Standalone tag_id index for correlated subqueries in tagItems() (WHERE mtm2.tag_id=t.id).
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_mtm_tag_id ON message_tag_map(tag_id)"));
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

    notifyDataChanged();
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

        // Decode common HTML entities that survive plain-text extraction.
        s.replace("&ndash;"_L1,  u"\u2013"_s, Qt::CaseInsensitive);
        s.replace("&mdash;"_L1,  u"\u2014"_s, Qt::CaseInsensitive);
        s.replace("&hellip;"_L1, u"\u2026"_s, Qt::CaseInsensitive);
        s.replace("&rsquo;"_L1,  u"\u2019"_s, Qt::CaseInsensitive);
        s.replace("&lsquo;"_L1,  u"\u2018"_s, Qt::CaseInsensitive);
        s.replace("&rdquo;"_L1,  u"\u201D"_s, Qt::CaseInsensitive);
        s.replace("&ldquo;"_L1,  u"\u201C"_s, Qt::CaseInsensitive);

        // Strip markdown link syntax [text](url) → keep just the link text.
        // Run before URL stripping so the full [text](url) form is matched.
        s.replace(kReSnippetMarkdownLink, "\\1"_L1);

        const bool hadUrl = kReSnippetUrl.match(s).hasMatch();
        s.replace(kReSnippetUrl, QString());
        s.replace(kReSnippetCharsetBoundary, QString());
        // Clean up any remaining [text]( fragments and empty parens left after URL stripping.
        s.replace(kReSnippetMarkdownLink, "\\1"_L1);
        s.replace(kReSnippetEmptyParens, " "_L1);
        s = normalizeSnippetWhitespace(s);

        // Strip runs of repeated separator characters (====, ----, ____, - - - -).
        s.replace(kReSnippetSeparatorRun, " "_L1);
        s = normalizeSnippetWhitespace(s);

        // Strip common web-view boilerplate (at start or embedded), including optional URLs.
        s.replace(kReSnippetViewEmailInBrowser, QString());
        s.replace(kReSnippetViewInBrowser, QString());
        s.replace(kReSnippetViewAsWebPage, QString());
        s = normalizeSnippetWhitespace(s);

        s.replace(kReSnippetTrailingPunct, QString());
        s = normalizeSnippetWhitespace(s);

        const QString t = s.toLower();
        const int alphaCount = s.count(kReHasLetters);
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
    const QString ccValue               = header.value(QStringLiteral("cc")).toString().trimmed();
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
            scheduleDataChangedSignal();
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
        INSERT INTO messages (account_email, logical_key, sender, recipient, subject, received_at, snippet, body_html, message_id_header, gm_msg_id, unread, list_unsubscribe, reply_to, return_path, auth_results, x_mailer, in_reply_to, references_header, esp_vendor, thread_id, gm_thr_id, has_tracking_pixel, cc, flagged)
        VALUES (:account_email, :logical_key, :sender, :recipient, :subject, :received_at, :snippet, :body_html, :message_id_header, :gm_msg_id, :unread, :list_unsubscribe, :reply_to, :return_path, :auth_results, :x_mailer, :in_reply_to, :references_header, :esp_vendor, :thread_id, :gm_thr_id, :has_tracking_pixel, :cc, :flagged)
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
                                END,
          cc = CASE
                 WHEN excluded.cc IS NOT NULL AND length(trim(excluded.cc)) > 0
                 THEN excluded.cc ELSE messages.cc END,
          flagged = MAX(messages.flagged, excluded.flagged)
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
    qCanon.bindValue(QStringLiteral(":cc"),                 ccValue.isEmpty() ? QVariant() : QVariant(ccValue));
    qCanon.bindValue(QStringLiteral(":flagged"),            header.value(QStringLiteral("flagged"), 0).toInt());
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
            { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(senderEmailValue, storedSenderUrl); }
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
            { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(recipientEmailValue, storedRecipientUrl); }
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
        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(recipientEmailValue); }  // CASE expression; evict to re-query
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
        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(senderEmailValue); }  // CASE expression; evict to re-query
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
        static const QRegularExpression tokenRe(QStringLiteral("\"([^\"]+)\"|([^\\s()]+)"));
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

    scheduleDataChangedSignal();
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
    // Fast path: return cached result pre-warmed before foldersChanged/dataChanged.
    {
        QMutexLocker lock(&m_tagItemsCacheMutex);
        if (m_tagItemsCacheValid)
            return m_tagItemsCache;
    }

    QVariantList out;
    const auto database = db();
    if (!database.isValid() || !database.isOpen()) return out;

    QHash<QString, QVariantMap> byLabel;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT t.normalized_name AS label,
               COALESCE(NULLIF(trim(t.name), ''), t.normalized_name) AS display_name,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT mtm2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_tag_map mtm2
                   JOIN messages cm2 ON cm2.id=mtm2.message_id AND cm2.account_email=mtm2.account_email
                   WHERE mtm2.tag_id=t.id
                   GROUP BY mtm2.account_email, thread_key
                 )
               ) AS total,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT mtm2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_tag_map mtm2
                   JOIN messages cm2 ON cm2.id=mtm2.message_id AND cm2.account_email=mtm2.account_email
                   WHERE mtm2.tag_id=t.id
                     AND EXISTS (
                       SELECT 1 FROM message_folder_map mfu
                       WHERE mfu.account_email=mtm2.account_email
                         AND mfu.message_id=mtm2.message_id
                         AND mfu.unread=1
                     )
                   GROUP BY mtm2.account_email, thread_key
                 )
               ) AS unread,
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
        SELECT (
                 SELECT COUNT(*)
                 FROM (
                   SELECT m2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_folder_map m2
                   JOIN messages cm2 ON cm2.id=m2.message_id AND cm2.account_email=m2.account_email
                   WHERE lower(m2.folder) LIKE '%/important'
                   GROUP BY m2.account_email, thread_key
                 )
               ) AS total,
               (
                 SELECT COUNT(*)
                 FROM (
                   SELECT m2.account_email,
                          COALESCE(NULLIF(trim(cm2.gm_thr_id),''), NULLIF(trim(cm2.thread_id),''), CAST(cm2.id AS TEXT)) AS thread_key
                   FROM message_folder_map m2
                   JOIN messages cm2 ON cm2.id=m2.message_id AND cm2.account_email=m2.account_email
                   WHERE lower(m2.folder) LIKE '%/important'
                     AND EXISTS (
                       SELECT 1 FROM message_folder_map u
                       WHERE u.account_email=m2.account_email
                         AND u.message_id=m2.message_id
                         AND u.unread=1
                     )
                   GROUP BY m2.account_email, thread_key
                 )
               ) AS unread
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

    // Store in cache.
    {
        QMutexLocker lock(&m_tagItemsCacheMutex);
        m_tagItemsCache = out;
        m_tagItemsCacheValid = true;
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

    if (QThread::currentThread() == thread()) {
        reloadFolders();
    } else {
        // Coalesce rapid upsertFolder calls (e.g. 16 folders at startup) into one reloadFolders.
        bool expected = false;
        if (m_pendingFoldersReload.compare_exchange_strong(expected, true)) {
            QMetaObject::invokeMethod(this, [this]() {
                m_pendingFoldersReload.store(false);
                reloadFolders();
            }, Qt::QueuedConnection);
        }
    }
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

    if (removedFolderRows > 0 || removedCanonicalRows > 0)
        scheduleDataChangedSignal();
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
        scheduleDataChangedSignal();
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
        scheduleDataChangedSignal();
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
        scheduleDataChangedSignal();
    }
}

void DataStore::markMessageFlagged(const QString &accountEmail, const QString &uid, bool flagged)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty() || uid.trimmed().isEmpty()) return;

    const int val = flagged ? 1 : 0;
    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET flagged=:val "
        "WHERE id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND uid=:uid"
        ")"_L1);
    qMsg.bindValue(":val"_L1, val);
    qMsg.bindValue(":acc"_L1, acc);
    qMsg.bindValue(":uid"_L1, uid);
    qMsg.exec();

    emit messageFlaggedChanged(acc, uid, flagged);
    if (qMsg.numRowsAffected() > 0)
        scheduleDataChangedSignal();
}

void DataStore::reconcileFlaggedUids(const QString &accountEmail, const QString &folder,
                                     const QStringList &flaggedUids)
{
    if (flaggedUids.isEmpty()) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) return;

    QStringList placeholders;
    placeholders.reserve(flaggedUids.size());
    for (int i = 0; i < flaggedUids.size(); ++i) placeholders << QStringLiteral(":u%1").arg(i);

    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET flagged=1 "
        "WHERE id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));
    qMsg.bindValue(":acc"_L1,    acc);
    qMsg.bindValue(":folder"_L1, folder);
    for (int i = 0; i < flaggedUids.size(); ++i)
        qMsg.bindValue(QStringLiteral(":u%1").arg(i), flaggedUids.at(i));
    qMsg.exec();

    if (qMsg.numRowsAffected() > 0)
        scheduleDataChangedSignal();
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
        scheduleDataChangedSignal();
}

QString DataStore::folderUidForMessageId(const QString &accountEmail, const QString &folder, qint64 messageId) const
{
    if (messageId <= 0) return {};
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};
    QSqlQuery q(database);
    q.prepare("SELECT uid FROM message_folder_map "
              "WHERE account_email=:acc AND lower(folder)=lower(:folder) AND message_id=:mid "
              "LIMIT 1"_L1);
    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":mid"_L1,    messageId);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString();
}

void DataStore::deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder, qint64 messageId)
{
    if (messageId <= 0) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    QSqlQuery q(database);
    q.prepare("DELETE FROM message_folder_map "
              "WHERE account_email=:acc AND lower(folder)=lower(:folder) AND message_id=:mid"_L1);
    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":mid"_L1,    messageId);
    q.exec();
    const int removed = q.numRowsAffected();
    QSqlQuery qOrphan(database);
    qOrphan.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
    if (removed > 0 || qOrphan.numRowsAffected() > 0)
        scheduleDataChangedSignal();
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
    scheduleDataChangedSignal();
}

QMap<QString, qint64> DataStore::lookupByMessageIdHeaders(const QString &accountEmail,
                                                             const QStringList &messageIdHeaders)
{
    QMap<QString, qint64> result;
    if (messageIdHeaders.isEmpty()) return result;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return result;

    // Build parameterized IN clause.
    QStringList placeholders;
    placeholders.reserve(messageIdHeaders.size());
    for (int i = 0; i < messageIdHeaders.size(); ++i)
        placeholders.push_back(QStringLiteral(":mid%1").arg(i));

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT message_id_header, id FROM messages "
        "WHERE account_email = :account_email "
        "  AND message_id_header IN (%1) "
        "  AND message_id_header IS NOT NULL "
        "  AND length(trim(message_id_header)) > 0"
    ).arg(placeholders.join(u',')));
    q.bindValue(u":account_email"_s, accountEmail);
    for (int i = 0; i < messageIdHeaders.size(); ++i)
        q.bindValue(placeholders[i], messageIdHeaders[i]);

    if (!q.exec()) return result;
    while (q.next())
        result.insert(q.value(0).toString(), q.value(1).toLongLong());
    return result;
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
        scheduleDataChangedSignal();
}

QStringList DataStore::folderUids(const QString &accountEmail, const QString &folder) const
{
    QStringList out;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    QSqlQuery q(database);
    // JOIN against messages so orphan edges (where the messages row was deleted
    // but the folder-map row wasn't) are excluded. The sync engine uses this list
    // to decide which UIDs are already local — orphan edges would make it skip
    // re-fetching messages that are effectively missing from the DB.
    q.prepare(QStringLiteral(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email "
        "WHERE mfm.account_email=:account_email AND lower(mfm.folder)=lower(:folder)"));
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

qint64 DataStore::folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return 0;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT last_sync_modseq FROM folder_sync_status "
        "WHERE account_email=:account_email AND lower(folder)=lower(:folder) LIMIT 1"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    if (!q.exec() || !q.next())
        return 0;
    bool ok = false;
    const qint64 v = q.value(0).toLongLong(&ok);
    return ok ? v : 0;
}

void DataStore::updateFolderLastSyncModSeq(const QString &accountEmail, const QString &folder,
                                           const qint64 modseq)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        INSERT INTO folder_sync_status(account_email, folder, last_sync_modseq, updated_at)
        VALUES(:account_email, :folder, :modseq, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          last_sync_modseq=excluded.last_sync_modseq,
          updated_at=datetime('now')
    )"));
    q.bindValue(QStringLiteral(":account_email"), accountEmail.trimmed());
    q.bindValue(QStringLiteral(":folder"), folder.trimmed());
    q.bindValue(QStringLiteral(":modseq"), modseq);
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
               ), 1)                                as thread_count,
               COALESCE(cm.cc, '')                  as cc,
               COALESCE(cm.flagged, 0)              as flagged
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
    row.insert(QStringLiteral("cc"),              q.value(25).toString());
    row.insert(QStringLiteral("flagged"),         q.value(26).toInt() == 1);
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
               COALESCE(cm.reply_to, '')  as reply_to,
               COALESCE(cm.cc, '')        as cc,
               COALESCE(cm.flagged, 0)    as flagged
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
        row[QStringLiteral("cc")]           = q.value(16).toString();
        row[QStringLiteral("flagged")]      = q.value(17).toInt() == 1;
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
    const bool hasHtmlish = kReHtmlish.match(html).hasMatch();
    const bool hasMarkdownLinks = kReMarkdownLinks.match(html).hasMatch();
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
        scheduleDataChangedSignal();

    if (changed)
        emit bodyHtmlUpdated(accountEmail.trimmed(), folder.trimmed(), uid.trimmed());

    return changed;
}

void DataStore::scheduleDataChangedSignal()
{
    bool expected = false;
    if (!m_reloadInboxScheduled.compare_exchange_strong(expected, true))
        return;

    QTimer::singleShot(300, this, [this]() {
        m_reloadInboxScheduled.store(false);
        notifyDataChanged();
    });
}

QStringList DataStore::statsKeysFromFolders() const
{
    QStringList keys;
    keys << QStringLiteral("favorites:all-inboxes")
         << QStringLiteral("favorites:unread")
         << QStringLiteral("favorites:flagged");
    for (const QVariant &fv : m_folders) {
        const QVariantMap f = fv.toMap();
        const QString rawName = f.value(QStringLiteral("name")).toString().trimmed();
        if (rawName.isEmpty()) continue;
        if (rawName.contains(QStringLiteral("/Categories/"), Qt::CaseInsensitive)) continue;
        // Normalize to match QML's normalizedRemoteFolderName: [Google Mail]/ → [Gmail]/,
        // but keep the [Gmail]/ prefix so the cache key matches what QML passes.
        QString norm = rawName.toLower();
        if (norm.startsWith("[google mail]/"_L1))
            norm = "[gmail]/"_L1 + norm.mid(14);
        keys << ("account:"_L1 + norm);
        if (f.value(QStringLiteral("specialUse")).toString().trimmed().isEmpty()
                && !norm.contains(QLatin1Char('/')))
            keys << (QStringLiteral("tag:") + norm);
    }
    keys.removeDuplicates();
    return keys;
}

void DataStore::warmStatsCacheThen(std::function<void()> callback)
{
    const QStringList keys = statsKeysFromFolders();
    // Invalidate stale entries we're about to recompute.
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        for (const QString &k : keys)
            m_folderStatsCache.remove(k);
    }
    {
        QMutexLocker lock(&m_tagItemsCacheMutex);
        m_tagItemsCacheValid = false;
    }
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [watcher, cb = std::move(callback)]() {
        watcher->deleteLater();
        cb();
    });
    watcher->setFuture(QtConcurrent::run([this, keys]() {
        for (const QString &key : keys)
            (void)statsForFolder(key, {});
        (void)tagItems();   // pre-warm tagItems cache so tagFolderItems() is instant on UI thread
    }));
}

void DataStore::notifyDataChanged()
{
    // Pre-warm stats cache on a worker thread, then emit dataChanged so QML
    // folderStatsByKey bindings (which fire on dataChanged) get instant cache hits.
    warmStatsCacheThen([this]() {
        emit dataChanged();
    });
}

QVariantList DataStore::messagesForSelection(const QString &folderKey,
                                             const QStringList &selectedCategories,
                                             int selectedCategoryIndex,
                                             int limit,
                                             int offset,
                                             bool *hasMore) const
{
    const QSqlDatabase database = db();
    const QString key = folderKey.trimmed();
    if (hasMore) *hasMore = false;

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

    if (key.startsWith(QStringLiteral("local:"), Qt::CaseInsensitive)) {
        if (hasMore) *hasMore = false;
        return {};
    }

    if (key.compare(QStringLiteral("favorites:flagged"), Qt::CaseInsensitive) == 0) {
        if (!database.isValid() || !database.isOpen()) return {};
        const int safeOffset = qMax(0, offset);
        QSqlQuery qFlagged(database);
        qFlagged.prepare(QStringLiteral(R"(
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
                   COALESCE(cm.gm_thr_id, '') as gm_thr_id,
                   COALESCE((SELECT GROUP_CONCAT(m2.sender, char(31))
                             FROM messages m2
                             WHERE m2.account_email = cm.account_email
                               AND m2.thread_id = cm.thread_id
                               AND cm.thread_id IS NOT NULL
                               AND length(trim(COALESCE(cm.thread_id, ''))) > 0), '') as all_senders,
                   COALESCE(cm.flagged, 0) as flagged
            FROM message_folder_map mfm
            JOIN messages cm ON cm.id = mfm.message_id
            WHERE cm.account_email=mfm.account_email
              AND cm.flagged=1
            GROUP BY cm.id
            ORDER BY cm.received_at DESC
            LIMIT :limit OFFSET :offset
        )"));
        qFlagged.bindValue(QStringLiteral(":limit"), limit > 0 ? limit + 1 : 5000);
        qFlagged.bindValue(QStringLiteral(":offset"), safeOffset);

        QVariantList out;
        if (qFlagged.exec()) {
            while (qFlagged.next()) {
                const QString tid  = qFlagged.value(15).toString().trimmed();
                QString gtid       = qFlagged.value(17).toString().trimmed();
                const QString acct = qFlagged.value(0).toString();
                const QString mid  = qFlagged.value(3).toString();
                if (gtid.isEmpty() && tid.startsWith(QStringLiteral("gm:"), Qt::CaseInsensitive))
                    gtid = tid.mid(3).trimmed();

                QVariantMap row;
                row.insert(QStringLiteral("accountEmail"), qFlagged.value(0));
                row.insert(QStringLiteral("folder"), qFlagged.value(1));
                row.insert(QStringLiteral("uid"), qFlagged.value(2));
                row.insert(QStringLiteral("messageId"), mid);
                row.insert(QStringLiteral("sender"), qFlagged.value(4));
                row.insert(QStringLiteral("subject"), qFlagged.value(5));
                row.insert(QStringLiteral("recipient"), qFlagged.value(6));
                row.insert(QStringLiteral("recipientAvatarUrl"), qFlagged.value(7));
                row.insert(QStringLiteral("receivedAt"), qFlagged.value(8));
                row.insert(QStringLiteral("snippet"),          qFlagged.value(9));
                row.insert(QStringLiteral("hasTrackingPixel"), qFlagged.value(10).toInt() == 1);
                row.insert(QStringLiteral("avatarDomain"),     qFlagged.value(11));
                row.insert(QStringLiteral("avatarUrl"), qFlagged.value(12));
                row.insert(QStringLiteral("avatarSource"), qFlagged.value(13));
                row.insert(QStringLiteral("unread"), qFlagged.value(14).toInt() == 1);
                row.insert(QStringLiteral("threadId"),    tid);
                row.insert(QStringLiteral("threadCount"), qFlagged.value(16).toInt());
                row.insert(QStringLiteral("gmThrId"),     gtid);
                row.insert(QStringLiteral("allSenders"),  qFlagged.value(18));
                row.insert(QStringLiteral("flagged"),     true);
                out.push_back(row);
            }
        }
        if (limit > 0 && out.size() > limit) {
            if (hasMore) *hasMore = true;
            out = out.mid(0, limit);
        } else {
            if (hasMore) *hasMore = false;
        }
        annotateMessageFlags(out);
        return out;
    }

    if (key.compare(QStringLiteral("favorites:all-inboxes"), Qt::CaseInsensitive) == 0
        || key.compare(QStringLiteral("favorites:unread"), Qt::CaseInsensitive) == 0) {
        QVariantList out;
        if (database.isValid() && database.isOpen()) {
            const bool unreadOnly = key.compare(QStringLiteral("favorites:unread"), Qt::CaseInsensitive) == 0;
            const int safeOffset = qMax(0, offset);
            const int chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
            const int targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

            QSet<QString> seenKeys;
            int rawOffset = 0;
            bool exhausted = false;
            while (!exhausted) {
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
                           COALESCE(cm.gm_thr_id, '') as gm_thr_id,
                           COALESCE((SELECT GROUP_CONCAT(m2.sender, char(31))
                                     FROM messages m2
                                     WHERE m2.account_email = cm.account_email
                                       AND m2.thread_id = cm.thread_id
                                       AND cm.thread_id IS NOT NULL
                                       AND length(trim(COALESCE(cm.thread_id, ''))) > 0), '') as all_senders,
                           COALESCE(cm.flagged, 0) as flagged
                    FROM message_folder_map mfm
                    JOIN messages cm ON cm.id = mfm.message_id
                    WHERE (
                        lower(mfm.folder)='inbox'
                        OR lower(mfm.folder)='[gmail]/inbox'
                        OR lower(mfm.folder)='[google mail]/inbox'
                        OR lower(mfm.folder) LIKE '%/inbox'
                    )
                      AND (:unread_only=0 OR mfm.unread=1)
                      AND NOT EXISTS (
                          SELECT 1 FROM message_folder_map t
                          WHERE t.account_email=mfm.account_email
                            AND t.message_id=mfm.message_id
                            AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
                      )
                    ORDER BY cm.received_at DESC
                    LIMIT :limit OFFSET :offset
                )"));
                q.bindValue(QStringLiteral(":unread_only"), unreadOnly ? 1 : 0);
                q.bindValue(QStringLiteral(":limit"), chunkSize);
                q.bindValue(QStringLiteral(":offset"), rawOffset);

                int fetchedRaw = 0;
                if (q.exec()) {
                    while (q.next()) {
                        ++fetchedRaw;

                        const QString tid   = q.value(15).toString().trimmed();
                        QString gtid        = q.value(17).toString().trimmed();
                        const QString acct  = q.value(0).toString();
                        const QString mid   = q.value(3).toString();
                        if (gtid.isEmpty() && tid.startsWith(QStringLiteral("gm:"), Qt::CaseInsensitive))
                            gtid = tid.mid(3).trimmed();
                        const QString tkey  = !gtid.isEmpty()
                            ? acct + QStringLiteral("|gtid:") + gtid
                            : (tid.isEmpty()
                                ? acct + QStringLiteral("|msg:") + mid
                                : acct + QStringLiteral("|tid:") + tid);
                        if (seenKeys.contains(tkey)) continue;
                        seenKeys.insert(tkey);

                        QVariantMap row;
                        row.insert(QStringLiteral("accountEmail"), q.value(0));
                        row.insert(QStringLiteral("folder"), q.value(1));
                        row.insert(QStringLiteral("uid"), q.value(2));
                        row.insert(QStringLiteral("messageId"), mid);
                        row.insert(QStringLiteral("sender"), q.value(4));
                        row.insert(QStringLiteral("subject"), q.value(5));
                        row.insert(QStringLiteral("recipient"), q.value(6));
                        row.insert(QStringLiteral("recipientAvatarUrl"), q.value(7));
                        row.insert(QStringLiteral("receivedAt"), q.value(8));
                        row.insert(QStringLiteral("snippet"),          q.value(9));
                        row.insert(QStringLiteral("hasTrackingPixel"), q.value(10).toInt() == 1);
                        row.insert(QStringLiteral("avatarDomain"),     q.value(11));
                        row.insert(QStringLiteral("avatarUrl"), q.value(12));
                        row.insert(QStringLiteral("avatarSource"), q.value(13));
                        row.insert(QStringLiteral("unread"), q.value(14).toInt() == 1);
                        row.insert(QStringLiteral("threadId"),    tid);
                        row.insert(QStringLiteral("threadCount"), q.value(16).toInt());
                        row.insert(QStringLiteral("gmThrId"),     gtid);
                        row.insert(QStringLiteral("allSenders"),  q.value(18));
                        row.insert(QStringLiteral("flagged"),     q.value(19).toInt() == 1);
                        out.push_back(row);

                        if (targetCount > 0 && out.size() >= targetCount)
                            break;
                    }
                }

                if (targetCount > 0 && out.size() >= targetCount)
                    break;
                if (fetchedRaw < chunkSize)
                    exhausted = true;
                rawOffset += fetchedRaw;
                if (fetchedRaw == 0)
                    exhausted = true;
            }

            if (limit > 0) {
                if (safeOffset >= out.size()) {
                    if (hasMore) *hasMore = false;
                    return {};
                }
                const int end = qMin(out.size(), safeOffset + limit);
                if (hasMore) *hasMore = (out.size() > safeOffset + limit);
                out = out.mid(safeOffset, end - safeOffset);
            } else {
                if (hasMore) *hasMore = false;
            }
        }

        annotateMessageFlags(out);
        return out;
    }

    QString selectedFolder;
    QString selectedTag;
    if (key.startsWith(QStringLiteral("account:"), Qt::CaseInsensitive)) {
        selectedFolder = key.mid(QStringLiteral("account:").size()).toLower();
    } else if (key.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive)) {
        selectedTag = key.mid(QStringLiteral("tag:").size()).toLower();
    }

    // Tag selections should resolve to folder-backed queries (DB source of truth),
    // not in-memory inbox snapshots.
    if (!selectedTag.isEmpty()) {
        selectedFolder = selectedTag;
        selectedTag.clear();
    }

    const bool categoryView = (selectedFolder == QStringLiteral("inbox")
                               && !selectedCategories.isEmpty()
                               && selectedCategoryIndex >= 0
                               && selectedCategoryIndex < selectedCategories.size());

    if (!selectedFolder.isEmpty() && !categoryView) {
        const bool selectedIsTrash = isTrashFolderName(selectedFolder);
        const bool selectedIsImportantPseudo = (selectedFolder == QStringLiteral("important"));

        // Folder views come directly from DB source-of-truth so large folders
        // like Trash/All Mail can page fully.
        QVariantList filtered;
        if (database.isValid() && database.isOpen()) {
            const int safeOffset = qMax(0, offset);
            const int chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
            const int targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

            QSet<QString> seenFolderTids;
            int rawOffset = 0;
            bool exhausted = false;

            while (!exhausted) {
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
                               COALESCE(cm.gm_thr_id, '') as gm_thr_id,
                               COALESCE((SELECT GROUP_CONCAT(m2.sender, char(31))
                                         FROM messages m2
                                         WHERE m2.account_email = cm.account_email
                                           AND m2.thread_id = cm.thread_id
                                           AND cm.thread_id IS NOT NULL
                                           AND length(trim(COALESCE(cm.thread_id, ''))) > 0), '') as all_senders,
                               COALESCE(cm.flagged, 0) as flagged
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id
                        WHERE ((:is_important=1 AND lower(mfm.folder) LIKE '%/important')
                               OR (:is_important=0 AND lower(mfm.folder)=:folder))
                        ORDER BY cm.received_at DESC
                        LIMIT :limit OFFSET :offset
                    )"));
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
                               COALESCE(cm.gm_thr_id, '') as gm_thr_id,
                               COALESCE((SELECT GROUP_CONCAT(m2.sender, char(31))
                                         FROM messages m2
                                         WHERE m2.account_email = cm.account_email
                                           AND m2.thread_id = cm.thread_id
                                           AND cm.thread_id IS NOT NULL
                                           AND length(trim(COALESCE(cm.thread_id, ''))) > 0), '') as all_senders,
                               COALESCE(cm.flagged, 0) as flagged
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id
                        WHERE ((:is_important=1 AND lower(mfm.folder) LIKE '%/important')
                               OR (:is_important=0 AND lower(mfm.folder)=:folder))
                          AND NOT EXISTS (
                              SELECT 1 FROM message_folder_map t
                              WHERE t.account_email=mfm.account_email
                                AND t.message_id=mfm.message_id
                                AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
                          )
                        ORDER BY cm.received_at DESC
                        LIMIT :limit OFFSET :offset
                    )"));
                }
                qFolder.bindValue(QStringLiteral(":folder"), selectedFolder);
                qFolder.bindValue(QStringLiteral(":is_important"), selectedIsImportantPseudo ? 1 : 0);
                qFolder.bindValue(QStringLiteral(":limit"), chunkSize);
                qFolder.bindValue(QStringLiteral(":offset"), rawOffset);

                int fetchedRaw = 0;
                if (qFolder.exec()) {
                    while (qFolder.next()) {
                        ++fetchedRaw;

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
                        row.insert(QStringLiteral("allSenders"),  qFolder.value(18));
                        row.insert(QStringLiteral("flagged"),     qFolder.value(19).toInt() == 1);
                        filtered.push_back(row);

                        if (targetCount > 0 && filtered.size() >= targetCount)
                            break;
                    }
                }

                if (targetCount > 0 && filtered.size() >= targetCount)
                    break;
                if (fetchedRaw < chunkSize)
                    exhausted = true;
                rawOffset += fetchedRaw;
                if (fetchedRaw == 0)
                    exhausted = true;
            }

            if (limit > 0) {
                if (safeOffset >= filtered.size()) {
                    if (hasMore) *hasMore = false;
                    return {};
                }
                const int end = qMin(filtered.size(), safeOffset + limit);
                if (hasMore) *hasMore = (filtered.size() > safeOffset + limit);
                filtered = filtered.mid(safeOffset, end - safeOffset);
            } else {
                if (hasMore) *hasMore = false;
            }
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
                       COALESCE(cm.gm_thr_id, '') as gm_thr_id,
                       COALESCE((SELECT GROUP_CONCAT(m2.sender, char(31))
                                 FROM messages m2
                                 WHERE m2.account_email = cm.account_email
                                   AND m2.thread_id = cm.thread_id
                                   AND cm.thread_id IS NOT NULL
                                   AND length(trim(COALESCE(cm.thread_id, ''))) > 0), '') as all_senders,
                       COALESCE(cm.flagged, 0) as flagged
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
                row.insert(QStringLiteral("allSenders"),  qRow.value(18));
                row.insert(QStringLiteral("flagged"),     qRow.value(19).toInt() == 1);
                out.push_back(row);
            }
        }

        annotateMessageFlags(out);
        return out;
    }

    return pageRows({});
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
    {
        QMutexLocker lock(&m_avatarCacheMutex);
        if (m_avatarCache.contains(e))
            return m_avatarCache.value(e);
    }

    auto database = db();
    if (database.isValid() && database.isOpen()) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("SELECT avatar_url FROM contact_avatars WHERE email=:email LIMIT 1"));
        q.bindValue(QStringLiteral(":email"), e);
        if (q.exec() && q.next()) {
            const QString cached = q.value(0).toString().trimmed();
            QMutexLocker lock(&m_avatarCacheMutex);
            m_avatarCache.insert(e, cached);
            return cached;
        }
    }

    QMutexLocker lock(&m_avatarCacheMutex);
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
    { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(e, storedUrl); }
}

bool DataStore::hasCachedHeadersForFolder(const QString &rawFolderName, int minCount) const
{
    const QString folder = rawFolderName.trimmed().toLower();
    if (folder.isEmpty()) return false;

    const auto database = db();
    if (!database.isValid() || !database.isOpen()) return false;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(R"(
        SELECT COUNT(DISTINCT mfm.message_id)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder)=:folder
    )"));
    q.bindValue(QStringLiteral(":folder"), folder);
    if (!q.exec() || !q.next()) return false;

    return q.value(0).toInt() >= qMax(1, minCount);
}

QVariantMap DataStore::statsForFolder(const QString &folderKey, const QString &rawFolderName) const
{
    QVariantMap out;
    int total = 0;
    int unread = 0;

    const QString key = folderKey.trimmed().toLower();

    // Fast path: return cached result (pre-warmed on worker thread before foldersChanged).
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        auto it = m_folderStatsCache.constFind(key);
        if (it != m_folderStatsCache.cend())
            return *it;
    }

    const auto database = db();
    if (!database.isValid() || !database.isOpen()) {
        out.insert(QStringLiteral("total"), 0);
        out.insert(QStringLiteral("unread"), 0);
        return out;
    }

    auto readCountPair = [&](QSqlQuery &q) {
        if (q.exec() && q.next()) {
            total = q.value(0).toInt();
            unread = q.value(1).toInt();
        }
    };
    if (key == QStringLiteral("favorites:all-inboxes") || key == QStringLiteral("favorites:unread")) {
        const bool unreadOnly = (key == QStringLiteral("favorites:unread"));
        QSqlQuery q(database);
        q.prepare(QStringLiteral(R"(
            SELECT COUNT(DISTINCT mfm.message_id),
                   SUM(CASE WHEN EXISTS (
                        SELECT 1 FROM message_folder_map x
                        WHERE x.account_email=mfm.account_email
                          AND x.message_id=mfm.message_id
                          AND x.unread=1
                   ) THEN 1 ELSE 0 END)
            FROM message_folder_map mfm
            WHERE (
                lower(mfm.folder)='inbox'
                OR lower(mfm.folder)='[gmail]/inbox'
                OR lower(mfm.folder)='[google mail]/inbox'
                OR lower(mfm.folder) LIKE '%/inbox'
            )
              AND (:unread_only=0 OR EXISTS (
                  SELECT 1 FROM message_folder_map u
                  WHERE u.account_email=mfm.account_email
                    AND u.message_id=mfm.message_id
                    AND u.unread=1
              ))
              AND NOT EXISTS (
                  SELECT 1 FROM message_folder_map t
                  WHERE t.account_email=mfm.account_email
                    AND t.message_id=mfm.message_id
                    AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
              )
        )"));
        q.bindValue(QStringLiteral(":unread_only"), unreadOnly ? 1 : 0);
        readCountPair(q);

        QSqlQuery qUnread(database);
        qUnread.prepare(QStringLiteral(R"(
            SELECT COUNT(*)
            FROM (
                SELECT mfm.account_email,
                       COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                FROM message_folder_map mfm
                JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                WHERE (
                    lower(mfm.folder)='inbox'
                    OR lower(mfm.folder)='[gmail]/inbox'
                    OR lower(mfm.folder)='[google mail]/inbox'
                    OR lower(mfm.folder) LIKE '%/inbox'
                )
                  AND (:unread_only=0 OR EXISTS (
                      SELECT 1 FROM message_folder_map u
                      WHERE u.account_email=mfm.account_email
                        AND u.message_id=mfm.message_id
                        AND u.unread=1
                  ))
                  AND NOT EXISTS (
                      SELECT 1 FROM message_folder_map t
                      WHERE t.account_email=mfm.account_email
                        AND t.message_id=mfm.message_id
                        AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
                  )
                  AND EXISTS (
                      SELECT 1 FROM message_folder_map x
                      WHERE x.account_email=mfm.account_email
                        AND x.message_id=mfm.message_id
                        AND x.unread=1
                  )
                GROUP BY mfm.account_email, thread_key
            )
        )"));
        qUnread.bindValue(QStringLiteral(":unread_only"), unreadOnly ? 1 : 0);
        if (qUnread.exec() && qUnread.next())
            unread = qUnread.value(0).toInt();

        QSqlQuery qTotal(database);
        qTotal.prepare(QStringLiteral(R"(
            SELECT COUNT(*)
            FROM (
                SELECT mfm.account_email,
                       COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                FROM message_folder_map mfm
                JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                WHERE (
                    lower(mfm.folder)='inbox'
                    OR lower(mfm.folder)='[gmail]/inbox'
                    OR lower(mfm.folder)='[google mail]/inbox'
                    OR lower(mfm.folder) LIKE '%/inbox'
                )
                  AND (:unread_only=0 OR EXISTS (
                      SELECT 1 FROM message_folder_map u
                      WHERE u.account_email=mfm.account_email
                        AND u.message_id=mfm.message_id
                        AND u.unread=1
                  ))
                  AND NOT EXISTS (
                      SELECT 1 FROM message_folder_map t
                      WHERE t.account_email=mfm.account_email
                        AND t.message_id=mfm.message_id
                        AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
                  )
                GROUP BY mfm.account_email, thread_key
            )
        )"));
        qTotal.bindValue(QStringLiteral(":unread_only"), unreadOnly ? 1 : 0);
        if (qTotal.exec() && qTotal.next())
            total = qTotal.value(0).toInt();

        if (unreadOnly)
            total = unread;
    } else if (key == QStringLiteral("favorites:flagged") || key.startsWith(QStringLiteral("local:"))) {
        total = 0;
        unread = 0;
    } else if (key.startsWith(QStringLiteral("tag:"))) {
        const QString tag = key.mid(QStringLiteral("tag:").size()).trimmed().toLower();
        if (!tag.isEmpty()) {
            QSqlQuery q(database);
            if (tag == QStringLiteral("important")) {
                q.prepare(QStringLiteral(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN EXISTS (
                                SELECT 1 FROM message_folder_map x
                                WHERE x.account_email=mfm.account_email
                                  AND x.message_id=mfm.message_id
                                  AND x.unread=1
                           ) THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder) LIKE '%/important'
                )"));
            } else {
                q.prepare(QStringLiteral(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN EXISTS (
                                SELECT 1 FROM message_folder_map x
                                WHERE x.account_email=mfm.account_email
                                  AND x.message_id=mfm.message_id
                                  AND x.unread=1
                           ) THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder)=:folder
                )"));
                q.bindValue(QStringLiteral(":folder"), tag);
            }
            readCountPair(q);

            QSqlQuery qUnread(database);
            if (tag == QStringLiteral("important")) {
                qUnread.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder) LIKE '%/important'
                          AND EXISTS (
                              SELECT 1 FROM message_folder_map x
                              WHERE x.account_email=mfm.account_email
                                AND x.message_id=mfm.message_id
                                AND x.unread=1
                          )
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
            } else {
                qUnread.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                          AND EXISTS (
                              SELECT 1 FROM message_folder_map x
                              WHERE x.account_email=mfm.account_email
                                AND x.message_id=mfm.message_id
                                AND x.unread=1
                          )
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                qUnread.bindValue(QStringLiteral(":folder"), tag);
            }
            if (qUnread.exec() && qUnread.next())
                unread = qUnread.value(0).toInt();

            QSqlQuery qTotal(database);
            if (tag == QStringLiteral("important")) {
                qTotal.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder) LIKE '%/important'
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
            } else {
                qTotal.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                qTotal.bindValue(QStringLiteral(":folder"), tag);
            }
            if (qTotal.exec() && qTotal.next())
                total = qTotal.value(0).toInt();
        }
    } else {
        QString folder = rawFolderName.trimmed().toLower();
        if (folder.isEmpty() && key.startsWith(QStringLiteral("account:")))
            folder = key.mid(QStringLiteral("account:").size());

        if (!folder.isEmpty()) {
            if (folder == QStringLiteral("inbox")) {
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
                readCountPair(q);

                QSqlQuery qUnread(database);
                qUnread.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
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
                          AND EXISTS (
                              SELECT 1 FROM message_folder_map x
                              WHERE x.account_email=mfm.account_email
                                AND x.message_id=mfm.message_id
                                AND x.unread=1
                          )
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                if (qUnread.exec() && qUnread.next())
                    unread = qUnread.value(0).toInt();

                QSqlQuery qTotal(database);
                qTotal.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
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
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                if (qTotal.exec() && qTotal.next())
                    total = qTotal.value(0).toInt();
            } else {
                QSqlQuery q(database);
                q.prepare(QStringLiteral(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN mfm.unread=1 THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder)=:folder
                )"));
                q.bindValue(QStringLiteral(":folder"), folder);
                readCountPair(q);

                QSqlQuery qUnread(database);
                qUnread.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                          AND EXISTS (
                              SELECT 1 FROM message_folder_map x
                              WHERE x.account_email=mfm.account_email
                                AND x.message_id=mfm.message_id
                                AND x.unread=1
                          )
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                qUnread.bindValue(QStringLiteral(":folder"), folder);
                if (qUnread.exec() && qUnread.next())
                    unread = qUnread.value(0).toInt();

                QSqlQuery qTotal(database);
                qTotal.prepare(QStringLiteral(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                        GROUP BY mfm.account_email, thread_key
                    )
                )"));
                qTotal.bindValue(QStringLiteral(":folder"), folder);
                if (qTotal.exec() && qTotal.next())
                    total = qTotal.value(0).toInt();
            }
        }
    }

    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("unread"), unread);

    // Cache for future calls (e.g. after pre-warm, subsequent delegate creations are instant).
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        m_folderStatsCache.insert(key, out);
    }
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

    // Pre-warm stats cache on worker thread so QML folderStats delegates see
    // instant cache hits when foldersChanged fires (no synchronous DB queries on UI thread).
    warmStatsCacheThen([this]() {
        emit foldersChanged();
    });
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
