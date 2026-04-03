#include "datastore.h"

#include "../utils.h"

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
#include <QFont>
#include <QPainter>
#include <limits>

#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

using namespace Qt::Literals::StringLiterals;

namespace {
const QRegularExpression kReWhitespace(R"(\s+)"_L1);
const QRegularExpression kReNonAlnumSplit("[^a-z0-9]+"_L1);
const QRegularExpression kReHasLetters("[A-Za-z]"_L1);
const QRegularExpression kReCsvSemicolonOutsideQuotes(R"([,;](?=(?:[^"]*"[^"]*")*[^"]*$))"_L1);
const QRegularExpression kReEmailAddress(R"(([A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}))"_L1, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kReSnippetUrl(R"(https?://\S+)"_L1, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kReSnippetCharsetBoundary(R"(\b(?:charset|boundary)\s*=\s*"?[^"\s]+"?)"_L1, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kReSnippetViewEmailInBrowser(R"((?i)view\s+(?:this\s+)?email\s+in\s+(?:a|your)?\s*browser[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetViewInBrowser(R"((?i)view\s+in\s+(?:a|your)?\s*browser[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetViewAsWebPage(R"((?i)view\s+as\s+(?:a\s+)?web\s+page[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetTrailingPunct(R"(\s*[(){}\[\]|:;.,-]+\s*$)"_L1);
const auto &kReHtmlish = Kestrel::htmlishRe();
const QRegularExpression kReMarkdownLinks(R"(\[[^\]\n]{1,240}\]\(https?://[^\s)]+\))"_L1, QRegularExpression::CaseInsensitiveOption);

// Strip markdown link syntax [text](url) → replacement captures just the link text.
// Handles [text](url), [text]( ) and [text]( (partial — closing paren is optional).
const QRegularExpression kReSnippetMarkdownLink( R"(\[([^\[\]\n]{1,160})\]\([^\)\n]{0,300}\)?)"_L1);

// Runs of 4+ repeated separator characters, or space-separated patterns like - - - - -.
const QRegularExpression kReSnippetSeparatorRun( R"([-=_*|#~<>]{4,}|(?:[-=_*|#~<>] ){3,}[-=_*|#~<>]?)"_L1);

// Leftover empty or whitespace-only parentheses after URL/link stripping.
const QRegularExpression kReSnippetEmptyParens(R"(\(\s*\))"_L1);

// Extract the first RFC 5322 Message-ID from a header value (e.g. References, In-Reply-To).
// Returns a normalized (lowercase, no angle-brackets) form.
QString
extractFirstMessageIdFromHeader(const QString &val) {
    if (val.trimmed().isEmpty()) { return {}; }

    static const QRegularExpression re("<([^>\\s]+)>"_L1);

    if (const auto m = re.match(val); m.hasMatch()) {
        return m.captured(1).trimmed().toLower();
    }

    // Bare message-id without angle brackets — take first whitespace-delimited token.
    const auto parts = val.trimmed().split(kReWhitespace, Qt::SkipEmptyParts);

    return parts.isEmpty() ? QString() : parts.first().toLower();
}

// Compute the canonical thread root ID from the three RFC 5322 threading headers.
// References chain → In-Reply-To → own Message-ID.
QString
computeThreadId(const QString &refs, const QString &irt, const QString &ownMsgId) {
    if (auto fromRefs = extractFirstMessageIdFromHeader(refs); !fromRefs.isEmpty()) {
        return fromRefs;
    }

    if (auto fromIrt = extractFirstMessageIdFromHeader(irt); !fromIrt.isEmpty()) {
        return fromIrt;
    }

    return extractFirstMessageIdFromHeader(ownMsgId);
}

QString
extractFirstEmail(const QString &raw) {
    const auto m = kReEmailAddress.match(raw);
    return m.hasMatch() ? Kestrel::normalizeEmail(m.captured(1)) : QString();
}

bool
computeHasTrackingPixel(const QString &bodyHtml, const QString &senderRaw) {
    if (bodyHtml.isEmpty()) { return false; }

    const auto email = extractFirstEmail(senderRaw);
    QString senderDomain;

    {
        const int at = static_cast<int>(email.lastIndexOf('@'));
        if (at >= 0) {
            senderDomain = email.mid(at + 1).toLower().trimmed();
            const int colon = static_cast<int>(senderDomain.indexOf(':'));
            if (colon > 0) senderDomain = senderDomain.left(colon);
        }
    }

    static const QRegularExpression imgTagRe("<img\\b[^>]*>"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression srcRe(R"(\bsrc\s*=\s*["'](https?://[^"']+)["'])"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression widthRe(R"(\bwidth\s*=\s*["']\s*1\s*["'])"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression heightRe(R"(\bheight\s*=\s*["']\s*1\s*["'])"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hostRe(R"(^https?://([^/?#]+))"_L1, QRegularExpression::CaseInsensitiveOption);

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
        const int colon = static_cast<int>(pixelHost.indexOf(':'));
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
    const QString email = Kestrel::normalizeEmail(emailRaw);
    if (name.isEmpty()) return std::numeric_limits<int>::min() / 4;

    int score = 0;
    const QString lower = name.toLower();

    if (name.contains(' ')) score += 3;
    if (name.size() >= 4 && name.size() <= 40) score += 2;

    const QStringList nameTokens = lower.split(kReNonAlnumSplit, Qt::SkipEmptyParts);
    if (nameTokens.size() >= 2) score += 4;

    if (!email.isEmpty()) {
        const int at = static_cast<int>(email.indexOf('@'));
        const QString local = (at > 0) ? email.left(at) : email;
        const QStringList localTokens = local.split(kReNonAlnumSplit, Qt::SkipEmptyParts);
        for (const QString &tok : localTokens) {
            if (tok.size() < 3) continue;
            if (nameTokens.contains(tok)) score += 8;
        }
        if (lower.contains(local) && local.size() >= 4) score += 6;
    }

    static const QSet<QString> generic = {
        "microsoft"_L1, "outlook"_L1, "gmail"_L1,
        "team"_L1, "support"_L1, "customer"_L1,
        "service"_L1, "notification"_L1, "admin"_L1,
        "info"_L1, "noreply"_L1, "no"_L1
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

    const QString email = Kestrel::normalizeEmail(knownEmail);
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
            const int lt2 = static_cast<int>(candidate.indexOf('<'));
            if (lt2 > 0) candidate = candidate.left(lt2).trimmed();
            candidate.remove('"');
            candidate.remove('\'');
            candidate = candidate.trimmed();
            if (candidate.isEmpty()) continue;
            if (kReEmailAddress.match(candidate).hasMatch()) continue;

            const QString scoreEmail = email.isEmpty() ? pEmail : email;
            if (!scoreEmail.isEmpty()) {
                const int at = static_cast<int>(scoreEmail.indexOf('@'));
                const QString local = (at > 0) ? scoreEmail.left(at) : scoreEmail;
                if (!local.isEmpty() && candidate.compare(local, Qt::CaseInsensitive) == 0) continue;
            }

            const int sc = displayNameScoreForEmail(candidate, scoreEmail);
            if (sc > bestScore) { bestScore = sc; best = candidate; }
        }
        if (!best.isEmpty()) return best;
    }

    const int lt = static_cast<int>(s.indexOf('<'));
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
        const int at = static_cast<int>(email.indexOf('@'));
        const QString local = (at > 0) ? email.left(at) : email;
        if (!local.isEmpty() && s.compare(local, Qt::CaseInsensitive) == 0) {
            // Hard fail: local-part mirror is not a valid display name candidate.
            return {};
        }
    }
    return s;
}

namespace {

int purgeCategoryFolderEdges(QSqlDatabase &database)
{
    QSqlQuery q(database);
    q.prepare(R"(
        DELETE FROM message_folder_map
        WHERE lower(folder) LIKE '%/categories/%'
    )"_L1);
    if (!q.exec()) return -1;
    return q.numRowsAffected();
}

QPair<int,int> repairTagProjectionInvariants(QSqlDatabase &database)
{
    int insertedTags = 0;
    int insertedMaps = 0;

    QSqlQuery q1(database);
    q1.prepare(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        SELECT ml.label, lower(ml.label), 'server', datetime('now')
        FROM message_labels ml
        LEFT JOIN tags t ON t.normalized_name = lower(ml.label)
        WHERE t.id IS NULL
        GROUP BY lower(ml.label)
    )"_L1);
    if (q1.exec()) insertedTags = q1.numRowsAffected();

    QSqlQuery q2(database);
    q2.prepare(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT ml.account_email, ml.message_id, t.id, 'server', datetime('now')
        FROM message_labels ml
        JOIN tags t ON t.normalized_name = lower(ml.label)
        LEFT JOIN message_tag_map mtm
          ON mtm.account_email = ml.account_email
         AND mtm.message_id = ml.message_id
         AND mtm.tag_id = t.id
        WHERE mtm.id IS NULL
    )"_L1);
    if (q2.exec()) insertedMaps = q2.numRowsAffected();

    return qMakePair(insertedTags, insertedMaps);
}

void logTagProjectionInvariants(QSqlDatabase &database)
{
    QSqlQuery q1(database);
    q1.prepare(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE NOT EXISTS (
            SELECT 1
            FROM tags t
            WHERE t.normalized_name = lower(ml.label)
        )
    )"_L1);
    if (q1.exec() && q1.next()) {
        (void)q1.value(0).toInt();
    }

    QSqlQuery q2(database);
    q2.prepare(R"(
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
    )"_L1);
    if (q2.exec() && q2.next()) {
        (void)q2.value(0).toInt();
    }

}

QPair<int,int> repairLabelEdgeInvariants(QSqlDatabase &database)
{
    int insertedEdges = 0;
    int insertedLabels = 0;

    QSqlQuery q1(database);
    q1.prepare(R"(
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
    )"_L1);
    if (q1.exec()) insertedEdges = q1.numRowsAffected();

    QSqlQuery q2(database);
    q2.prepare(R"(
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
    )"_L1);
    if (q2.exec()) insertedLabels = q2.numRowsAffected();

    return qMakePair(insertedEdges, insertedLabels);
}

void logLabelEdgeInvariants(QSqlDatabase &database)
{
    QSqlQuery q1(database);
    q1.prepare(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )"_L1);
    if (q1.exec() && q1.next()) {
        (void)q1.value(0).toInt();
    }

    QSqlQuery q2(database);
    q2.prepare(R"(
        SELECT count(*)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
    )"_L1);
    if (q2.exec() && q2.next()) {
        (void)q2.value(0).toInt();
    }

}

QString logicalMessageKey(const QString &accountEmail,
                         const QString &sender,
                         const QString &subject,
                         const QString &receivedAt)
{
    const QString normalized = Kestrel::normalizeEmail(accountEmail) + "\x1f"_L1
            + sender.trimmed().toLower() + "\x1f"_L1
            + subject.trimmed().toLower() + "\x1f"_L1
            + receivedAt.trimmed();
    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool isTrashFolderName(const QString &folder)
{
    const QString f = folder.trimmed().toLower();
    if (f.isEmpty()) return false;
    return f == "trash"_L1
            || f == "[gmail]/trash"_L1
            || f == "[google mail]/trash"_L1
            || f.endsWith("/trash"_L1);
}

bool isCategoryFolderName(const QString &folder)
{
    return folder.trimmed().toLower().contains("/categories/"_L1);
}

// Map raw X-GM-LABELS text to a synthetic category folder name using the same
// keyword matching as Imap::Parser::extractGmailCategoryFolder.
QString inferCategoryFromLabels(const QString &rawLabels)
{
    QString l = rawLabels.toLower();
    l.replace('"', ' ');  l.replace('\\', ' ');  l.replace('^', ' ');
    l.replace(':', ' ');  l.replace('/', ' ');    l.replace('-', ' ');

    auto has = [&](const char *needle) { return l.contains(QString::fromLatin1(needle)); };

    if (has("promotions") || has("promotion") || has("categorypromotions") || has("smartlabel_promo"))
        return "[Gmail]/Categories/Promotions"_L1;
    if (has("social") || has("categorysocial") || has("smartlabel_social"))
        return "[Gmail]/Categories/Social"_L1;
    if (has("purchases") || has("purchase") || has("categorypurchases") || has("smartlabel_receipt"))
        return "[Gmail]/Categories/Purchases"_L1;
    if (has("updates") || has("update") || has("categoryupdates") || has("smartlabel_notification"))
        return "[Gmail]/Categories/Updates"_L1;
    if (has("forums") || has("forum") || has("categoryforums") || has("smartlabel_group"))
        return "[Gmail]/Categories/Forums"_L1;
    if (has("primary") || has("categorypersonal") || has("smartlabel_personal"))
        return "[Gmail]/Categories/Primary"_L1;
    return {};
}

bool isSystemLabelName(const QString &label)
{
    const QString l = label.trimmed().toLower();
    if (l.isEmpty()) return true;
    return l == "inbox"_L1
            || l == "sent"_L1
            || l == "sent mail"_L1
            || l == "draft"_L1
            || l == "drafts"_L1
            || l == "trash"_L1
            || l == "spam"_L1
            || l == "junk"_L1
            || l == "all mail"_L1
            || l == "starred"_L1
            || l == "[gmail]"_L1
            || l == "[google mail]"_L1
            || l.startsWith("[gmail]/"_L1)
            || l.startsWith("[google mail]/"_L1)
            || l.startsWith("\\"_L1);
}

int folderEdgeCount(QSqlDatabase &database, const QString &accountEmail, const QString &folder)
{
    QSqlQuery q(database);
    // JOIN against messages so orphan edges (message row deleted but edge remains)
    // are excluded — keeps the count consistent with what the sync engine actually has.
    q.prepare(R"(
        SELECT COUNT(*)
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email
        WHERE mfm.account_email=:account_email AND lower(mfm.folder)=:folder
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed().toLower());
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
    q.prepare(R"(
        SELECT COUNT(*)
        FROM message_folder_map
        WHERE account_email=:account_email
          AND lower(folder)=:folder
          AND uid IN (%1)
    )"_L1.arg(placeholders.join(","_L1)));
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed().toLower());
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
    q.prepare(R"(
        SELECT COUNT(*)
        FROM message_folder_map
        WHERE account_email=:account_email
          AND (
            lower(folder)='[gmail]/all mail'
            OR lower(folder)='[google mail]/all mail'
            OR lower(folder) LIKE '%/all mail'
          )
          AND uid IN (%1)
    )"_L1.arg(placeholders.join(","_L1)));
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    for (int i = 0; i < uids.size(); ++i) {
        q.bindValue(QStringLiteral(":u%1").arg(i), uids.at(i));
    }
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

// Returns true if a new row was inserted (not just updated).
bool upsertFolderEdge(QSqlDatabase &database,
                      const QString &accountEmail,
                      int messageId,
                      const QString &folder,
                      const QString &uid,
                      const QVariant &unread)
{
    QSqlQuery qCheck(database);
    qCheck.prepare("SELECT 1 FROM message_folder_map WHERE account_email=:a AND folder=:f AND uid=:u"_L1);
    qCheck.bindValue(":a"_L1, accountEmail);
    qCheck.bindValue(":f"_L1, folder);
    qCheck.bindValue(":u"_L1, uid);
    qCheck.exec();
    const bool isNew = !qCheck.next();

    QSqlQuery qMapAuth(database);
    qMapAuth.prepare(R"(
        INSERT INTO message_folder_map (account_email, message_id, folder, uid, unread, source, confidence, observed_at)
        VALUES (:account_email, :message_id, :folder, :uid, :unread, 'imap-label', 100, datetime('now'))
        ON CONFLICT(account_email, folder, uid) DO UPDATE SET
          message_id=excluded.message_id,
          unread=MIN(message_folder_map.unread, excluded.unread),
          source='imap-label',
          confidence=MAX(message_folder_map.confidence, 100),
          observed_at=datetime('now')
    )"_L1);
    qMapAuth.bindValue(":account_email"_L1, accountEmail);
    qMapAuth.bindValue(":message_id"_L1, messageId);
    qMapAuth.bindValue(":folder"_L1, folder);
    qMapAuth.bindValue(":uid"_L1, uid);
    qMapAuth.bindValue(":unread"_L1, unread);
    qMapAuth.exec();

    return isNew;
}

int deleteFolderEdge(QSqlDatabase &database,
                     const QString &accountEmail,
                     const QString &folder,
                     const QString &uid)
{
    QSqlQuery qMap(database);
    qMap.prepare(R"(
        DELETE FROM message_folder_map
        WHERE account_email=:account_email AND folder=:folder AND uid=:uid
    )"_L1);
    qMap.bindValue(":account_email"_L1, accountEmail);
    qMap.bindValue(":folder"_L1, folder);
    qMap.bindValue(":uid"_L1, uid);
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
        q.prepare("DELETE FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"_L1);
        q.bindValue(":account_email"_L1, accountEmail);
        q.bindValue(":folder"_L1, folder);
        q.exec();
        return q.numRowsAffected();
    }

    // Compute set-difference in C++ to avoid a huge NOT IN (...) clause.
    // A typical reconcile prune removes 0-10 UIDs from a mailbox of thousands,
    // so building the delete set in application memory is far cheaper than
    // sending a 30KB+ SQL string to SQLite and binding thousands of parameters.
    QSqlQuery qLocal(database);
    qLocal.prepare("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"_L1);
    qLocal.bindValue(":account_email"_L1, accountEmail);
    qLocal.bindValue(":folder"_L1, folder);
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

    const QString sql = 
        "DELETE FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder) AND uid IN (%1)"_L1
        .arg(placeholders.join(","_L1));
    QSqlQuery qDel(database);
    qDel.prepare(sql);
    qDel.bindValue(":account_email"_L1, accountEmail);
    qDel.bindValue(":folder"_L1, folder);
    for (int i = 0; i < toDelete.size(); ++i)
        qDel.bindValue(QStringLiteral(":d%1").arg(i), toDelete.at(i));
    qDel.exec();
    return qDel.numRowsAffected();
}

}

} // namespace

DataStore::DataStore(QObject *parent)
    : QObject(parent)
    , m_connectionName("kestrel_%1"_L1.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
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
            + "/kestrel-mail"_L1;
    QDir().mkpath(base);
    const QString path = base + "/mail.db"_L1;

    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE"_L1, m_connectionName);
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
    q.exec("PRAGMA journal_mode=WAL"_L1);
    q.exec("PRAGMA busy_timeout=5000"_L1);
    q.exec("PRAGMA synchronous=NORMAL"_L1);

    // Finalized schema cleanup:
    // - drop legacy pre-refactor messages table
    // - rename canonical_messages -> messages
    if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='canonical_messages'"_L1)
            && q.next()) {
        q.exec("DROP TABLE IF EXISTS messages"_L1);
        q.exec("ALTER TABLE canonical_messages RENAME TO messages"_L1);
    }

    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }

    // Forward-compatible migration for existing DBs.
    q.exec("ALTER TABLE messages ADD COLUMN avatar_domain TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN avatar_url TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN avatar_source TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN message_id_header TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN gm_msg_id TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN recipient TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN recipient_avatar_url TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN list_unsubscribe TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN reply_to TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN return_path TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN auth_results TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN x_mailer TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN in_reply_to TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN references_header TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN esp_vendor TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN thread_id TEXT"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_thread ON messages(account_email, thread_id)"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN gm_thr_id TEXT"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_gm_thr ON messages(account_email, gm_thr_id)"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN has_tracking_pixel INTEGER DEFAULT 0"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN cc TEXT"_L1);
    q.exec("ALTER TABLE messages ADD COLUMN flagged INTEGER DEFAULT 0"_L1);
    q.exec("ALTER TABLE folder_sync_status ADD COLUMN last_sync_modseq INTEGER NOT NULL DEFAULT 0"_L1);

    // Backfill thread_id for existing messages (new ones get it in upsertHeader).
    {
        QSqlQuery bfQ(database);
        bfQ.exec(QStringLiteral(
            "SELECT id, message_id_header, in_reply_to, references_header "
            "FROM messages WHERE thread_id IS NULL OR length(trim(thread_id)) = 0 LIMIT 10000"
        ));
        QSqlQuery upQ(database);
        upQ.prepare("UPDATE messages SET thread_id=:tid WHERE id=:id"_L1);
        while (bfQ.next()) {
            const QString tid = computeThreadId(
                bfQ.value(3).toString(), bfQ.value(2).toString(), bfQ.value(1).toString());
            if (!tid.isEmpty()) {
                upQ.bindValue(":tid"_L1, tid);
                upQ.bindValue(":id"_L1, bfQ.value(0).toInt());
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
        if (qSel.exec("SELECT email, avatar_url FROM contact_avatars WHERE avatar_url LIKE 'data:%'"_L1)) {
            struct MigrateRow { QString email; QString url; };
            QVector<MigrateRow> rows;
            while (qSel.next())
                rows.push_back({qSel.value(0).toString(), qSel.value(1).toString()});
            for (const auto &row : rows) {
                const QString fileUrl = writeAvatarDataUri(row.email, row.url);
                QSqlQuery qUp(database);
                qUp.prepare(
                    "UPDATE contact_avatars SET avatar_url=:url WHERE email=:email"_L1);
                qUp.bindValue(":url"_L1, fileUrl);  // empty on failure → clears blob
                qUp.bindValue(":email"_L1, row.email);
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

    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }
    q.exec("ALTER TABLE message_folder_map ADD COLUMN unread INTEGER NOT NULL DEFAULT 1"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS contact_avatars (
          email TEXT PRIMARY KEY,
          avatar_url TEXT,
          source TEXT,
          last_checked_at TEXT NOT NULL DEFAULT (datetime('now')),
          etag TEXT,
          failure_count INTEGER NOT NULL DEFAULT 0
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS contact_display_names (
          email TEXT PRIMARY KEY,
          display_name TEXT NOT NULL,
          source TEXT,
          display_score INTEGER NOT NULL DEFAULT 0,
          last_seen_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }
    q.exec("ALTER TABLE contact_display_names ADD COLUMN display_score INTEGER NOT NULL DEFAULT 0"_L1);

    // Migration scaffold: explicit label/provenance store (eM-style direction).
    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }

    // eM-inspired normalization: keep per-message address rows to avoid global
    // display-name poisoning and to preserve message-local participant evidence.
    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }

    // Unified global tags (client + server-origin labels) inspired by mature client schemas.
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS tags (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          name TEXT NOT NULL,
          normalized_name TEXT NOT NULL UNIQUE,
          color TEXT,
          origin TEXT NOT NULL DEFAULT 'server',
          created_at TEXT NOT NULL DEFAULT (datetime('now')),
          updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }


    // Backfill unified tags from observed labels.
    q.exec(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        SELECT ml.label, lower(ml.label), 'server', datetime('now')
        FROM message_labels ml
        GROUP BY lower(ml.label)
        ON CONFLICT(normalized_name) DO UPDATE SET
          updated_at=datetime('now')
    )"_L1);

    q.exec(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT ml.account_email, ml.message_id, t.id, 'server', datetime('now')
        FROM message_labels ml
        JOIN tags t ON t.normalized_name = lower(ml.label)
        ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
          observed_at=datetime('now')
    )"_L1);


    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS folders (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          account_email TEXT NOT NULL,
          name TEXT NOT NULL,
          flags TEXT,
          special_use TEXT,
          UNIQUE(account_email, name)
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS folder_sync_status (
          account_email TEXT NOT NULL,
          folder TEXT NOT NULL,
          uid_next INTEGER,
          highest_modseq INTEGER,
          messages INTEGER,
          updated_at TEXT NOT NULL DEFAULT (datetime('now')),
          PRIMARY KEY(account_email, folder)
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS sender_image_permissions (
          domain TEXT PRIMARY KEY,
          granted_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
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
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS favorites_config (
          key     TEXT PRIMARY KEY,
          enabled INTEGER NOT NULL DEFAULT 1
        )
    )"_L1)) {
        return false;
    }

    // Seed defaults: All Inboxes / Unread / Flagged visible; rest hidden.
    q.exec(R"(
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
    )"_L1);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS user_folders (
          id         INTEGER PRIMARY KEY AUTOINCREMENT,
          name       TEXT    NOT NULL UNIQUE,
          sort_order INTEGER NOT NULL DEFAULT 0,
          created_at TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS search_history (
          id          INTEGER PRIMARY KEY AUTOINCREMENT,
          query       TEXT    NOT NULL UNIQUE,
          searched_at TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )"_L1)) {
        return false;
    }

    // Paging/list performance indexes.
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_received_at_id ON messages(received_at DESC, id DESC)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_folder_message ON message_folder_map(folder, message_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_message ON message_folder_map(account_email, message_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_folder_uid ON message_folder_map(account_email, folder, uid)"_L1);
    // Expression index so lower(folder) comparisons in fetchCandidatesForMessageKey use the index
    // instead of a full table scan (lower() on a plain-column index is not usable by SQLite).
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_lf_uid ON message_folder_map(account_email, lower(folder), uid)"_L1);
    // Standalone lower(folder) index for statsForFolder() WHERE lower(folder)=:f queries.
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_lower_folder ON message_folder_map(lower(folder))"_L1);
    // Covering index for EXISTS(...unread=1) subqueries in statsForFolder().
    q.exec("CREATE INDEX IF NOT EXISTS idx_mfm_account_message_unread ON message_folder_map(account_email, message_id, unread)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_ml_account_message_label ON message_labels(account_email, message_id, label)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_ml_label_lower ON message_labels(lower(label))"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mtm_account_message_tag ON message_tag_map(account_email, message_id, tag_id)"_L1);
    // Standalone tag_id index for correlated subqueries in tagItems() (WHERE mtm2.tag_id=t.id).
    q.exec("CREATE INDEX IF NOT EXISTS idx_mtm_tag_id ON message_tag_map(tag_id)"_L1);
    q.exec("CREATE INDEX IF NOT EXISTS idx_mp_address_lower ON message_participants(lower(address))"_L1);

    // Cleanup: remove poisoned display names that look like address lists/emails.
    q.exec(R"(
        DELETE FROM contact_display_names
        WHERE instr(display_name, '@') > 0
           OR instr(display_name, ',') > 0
           OR instr(display_name, ';') > 0
    )"_L1);

    // Migration cleanup: remove category folder edges (categories are labels/tags only).
    const int purgedCategoryEdges = purgeCategoryFolderEdges(database);
    if (purgedCategoryEdges > 0) {
        qInfo().noquote() << "[migration-cleanup]" << "purgedCategoryEdges=" << purgedCategoryEdges;
    }

    // Backfill missing/empty contact display names from participant evidence.
    q.exec(R"(
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
    )"_L1);

    // Repair pass: if a weak/empty canonical message row was created for a uid that
    // already maps to a stronger identified message, re-point the edge and let orphan
    // cleanup remove the weak row.
    q.exec(R"(
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
    )"_L1);

    notifyDataChanged();
    reloadFolders();
    return true;
}

bool DataStore::quickCheck() const
{
    QSqlDatabase database = db();
    if (!database.isOpen())
        return false;

    QSqlQuery q(database);
    if (!q.exec("PRAGMA quick_check"_L1)) {
        qWarning() << "[DataStore] quick_check failed to execute:" << q.lastError().text();
        return false;
    }

    if (q.next()) {
        const QString result = q.value(0).toString().trimmed().toLower();
        if (result == "ok"_L1) {
            qInfo() << "[DataStore] quick_check passed";
            return true;
        }
        qWarning() << "[DataStore] quick_check FAILED:" << q.value(0).toString();
        return false;
    }

    qWarning() << "[DataStore] quick_check returned no rows";
    return false;
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
        const int alphaCount = static_cast<int>(s.count(kReHasLetters));
        const bool danglingShort = s.endsWith('(') || s.endsWith(':') || s.endsWith('-');
        const bool junk = s.isEmpty()
                || t.startsWith("* "_L1)
                || t.contains(" fetch ("_L1)
                || t.contains("body[header.fields"_L1)
                || t.contains("x-gm-labels"_L1)
                || t.contains("body[text]"_L1)
                || t.contains("ok success"_L1)
                || t.contains("throttled"_L1)
                || t.contains("this is a multi-part message in mime format"_L1)
                || t.contains("view this email in your browser"_L1)
                || t.contains("view as a web page"_L1)
                || t.contains("it looks like your email client might not support html"_L1)
                || t.contains("try opening this email in another email client"_L1)
                || (hadUrl && (alphaCount < 20 || danglingShort));
        if (junk) {
            const QString subject = normalizeSnippetWhitespace(subjectRaw);
            if (!subject.isEmpty()) return subject.left(140);
            if (!originalNormalized.isEmpty()) return originalNormalized.left(140);
            return QString();
        }
        return s.left(140);
    };

    const QString accountEmail = header.value("accountEmail"_L1).toString();
    const QString folderValue = header.value("folder"_L1, "INBOX"_L1).toString();
    const QString uidValue = header.value("uid"_L1).toString();
    const QString subjectValue = header.value("subject"_L1).toString();
    const QString rawSnippetValue = header.value("snippet"_L1).toString();
    const QString senderValue = header.value("sender"_L1).toString();
    const QString recipientValue = header.value("recipient"_L1).toString();
    const QString recipientAvatarUrlValue = header.value("recipientAvatarUrl"_L1).toString().trimmed();
    const bool recipientAvatarLookupMiss = header.value("recipientAvatarLookupMiss"_L1, false).toBool();
    const QString senderEmailValue = extractFirstEmail(senderValue);
    const QString recipientEmailValue = extractFirstEmail(recipientValue);
    const QString senderDisplayNameValue = extractExplicitDisplayName(senderValue, senderEmailValue);
    const QString recipientDisplayNameValue = extractExplicitDisplayName(recipientValue, recipientEmailValue);
    const QString receivedAtValue = header.value("receivedAt"_L1, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).toString();
    const QString snippetValue = sanitizeSnippet(rawSnippetValue, subjectValue);
    const QString bodyHtmlValue = header.value("bodyHtml"_L1).toString().trimmed();
    const QString avatarUrlValue = header.value("avatarUrl"_L1).toString().trimmed();
    const QString avatarSourceValue = header.value("avatarSource"_L1).toString().trimmed().toLower();
    const int unreadValue = header.value("unread"_L1, true).toBool() ? 1 : 0;
    const QString messageIdHeaderValue = header.value("messageIdHeader"_L1).toString().trimmed();
    const QString gmMsgIdValue = header.value("gmMsgId"_L1).toString().trimmed();
    const QString gmThrIdValue = header.value("gmThrId"_L1).toString().trimmed();
    const QString listUnsubscribeValue  = header.value("listUnsubscribe"_L1).toString().trimmed();
    const QString replyToValue          = header.value("replyTo"_L1).toString().trimmed();
    const QString returnPathValue       = header.value("returnPath"_L1).toString().trimmed();
    const QString authResultsValue      = header.value("authResults"_L1).toString().trimmed();
    const QString xMailerValue          = header.value("xMailer"_L1).toString().trimmed();
    const QString inReplyToValue        = header.value("inReplyTo"_L1).toString().trimmed();
    const QString referencesValue       = header.value("references"_L1).toString().trimmed();
    const QString espVendorValue        = header.value("espVendor"_L1).toString().trimmed();
    const QString ccValue               = header.value("cc"_L1).toString().trimmed();
    const bool primaryLabelObserved = header.value("primaryLabelObserved"_L1, false).toBool();
    const QString rawGmailLabels = header.value("rawGmailLabels"_L1).toString();

    // Guardrail: avoid creating synthetic/empty canonical rows when we only have
    // a folder+uid edge (common during on-demand hydration/category relabel paths).
    const bool weakIdentity = senderValue.trimmed().isEmpty()
            && subjectValue.trimmed().isEmpty()
            && messageIdHeaderValue.isEmpty()
            && gmMsgIdValue.isEmpty();
    if (weakIdentity && !uidValue.trimmed().isEmpty()) {
        QSqlQuery qExisting(database);
        qExisting.prepare(R"(
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
        )"_L1);
        qExisting.bindValue(":account_email"_L1, accountEmail);
        qExisting.bindValue(":folder"_L1, folderValue);
        qExisting.bindValue(":uid"_L1, uidValue);
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
                qBody.prepare(R"(
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
                )"_L1);
                qBody.bindValue(":body_html"_L1, bodyHtmlValue);
                qBody.bindValue(":has_tp"_L1, hasTP);
                qBody.bindValue(":unread"_L1, unreadValue);
                qBody.bindValue(":message_id"_L1, existingMessageId);
                qBody.bindValue(":account_email"_L1, accountEmail);
                qBody.exec();
            }

            if (upsertFolderEdge(database, accountEmail, existingMessageId, folderValue, uidValue, unreadValue))
                incrementNewMessageCount(folderValue);
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
    if (subjectNormForLog.compare("T-Minus 26 Days Until Spring"_L1, Qt::CaseInsensitive) == 0
            || (sanNormForLog.compare(subjectNormForLog, Qt::CaseInsensitive) == 0 && !rawNormForLog.isEmpty()
                && rawNormForLog.compare(subjectNormForLog, Qt::CaseInsensitive) != 0)) {
    }

    QSqlQuery qCanon(database);
    qCanon.prepare(R"(
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
    )"_L1);
    qCanon.bindValue(":account_email"_L1, accountEmail);
    qCanon.bindValue(":logical_key"_L1, lkey);
    qCanon.bindValue(":sender"_L1, senderValue);
    qCanon.bindValue(":recipient"_L1, recipientValue);
    qCanon.bindValue(":subject"_L1, subjectValue);
    qCanon.bindValue(":received_at"_L1, receivedAtValue);
    qCanon.bindValue(":snippet"_L1, snippetValue);
    qCanon.bindValue(":body_html"_L1, bodyHtmlValue);
    qCanon.bindValue(":message_id_header"_L1, messageIdHeaderValue);
    qCanon.bindValue(":gm_msg_id"_L1, gmMsgIdValue);
    qCanon.bindValue(":unread"_L1, unreadValue);
    qCanon.bindValue(":list_unsubscribe"_L1,    listUnsubscribeValue.isEmpty() ? QVariant() : QVariant(listUnsubscribeValue));
    qCanon.bindValue(":reply_to"_L1,           replyToValue.isEmpty()        ? QVariant() : QVariant(replyToValue));
    qCanon.bindValue(":return_path"_L1,        returnPathValue.isEmpty()     ? QVariant() : QVariant(returnPathValue));
    qCanon.bindValue(":auth_results"_L1,       authResultsValue.isEmpty()    ? QVariant() : QVariant(authResultsValue));
    qCanon.bindValue(":x_mailer"_L1,           xMailerValue.isEmpty()        ? QVariant() : QVariant(xMailerValue));
    qCanon.bindValue(":in_reply_to"_L1,        inReplyToValue.isEmpty()      ? QVariant() : QVariant(inReplyToValue));
    qCanon.bindValue(":references_header"_L1,  referencesValue.isEmpty()     ? QVariant() : QVariant(referencesValue));
    qCanon.bindValue(":esp_vendor"_L1,         espVendorValue.isEmpty()      ? QVariant() : QVariant(espVendorValue));
    qCanon.bindValue(":gm_thr_id"_L1,          gmThrIdValue.isEmpty()         ? QVariant() : QVariant(gmThrIdValue));
    qCanon.bindValue(":has_tracking_pixel"_L1, computeHasTrackingPixel(bodyHtmlValue, senderValue) ? 1 : 0);
    qCanon.bindValue(":cc"_L1,                 ccValue.isEmpty() ? QVariant() : QVariant(ccValue));
    qCanon.bindValue(":flagged"_L1,            header.value("flagged"_L1, 0).toInt());
    {
        // For Gmail messages with X-GM-THRID, use it as the canonical thread ID.
        // This groups all messages in the same Gmail conversation correctly even
        // when References/In-Reply-To headers are missing or broken.
        QString threadIdValue;
        if (!gmThrIdValue.isEmpty())
            threadIdValue = "gm:"_L1 + gmThrIdValue;
        else
            threadIdValue = computeThreadId(referencesValue, inReplyToValue, messageIdHeaderValue);
        qCanon.bindValue(":thread_id"_L1, threadIdValue.isEmpty() ? QVariant() : QVariant(threadIdValue));
    }
    qCanon.exec();

    // When a Gmail thread ID is known, update ALL sibling messages in the same
    // conversation that haven't been assigned this thread_id yet. This fixes
    // existing messages that were synced before X-GM-THRID was added to the fetch.
    if (!gmThrIdValue.isEmpty()) {
        const QString newTid = "gm:"_L1 + gmThrIdValue;
        QSqlQuery sibQ(database);
        sibQ.prepare(
            "UPDATE messages SET thread_id=:tid WHERE account_email=:acct "
            "AND gm_thr_id=:gm_thr_id AND (thread_id IS NULL OR thread_id != :tid)"_L1);
        sibQ.bindValue(":tid"_L1,      newTid);
        sibQ.bindValue(":acct"_L1,     accountEmail);
        sibQ.bindValue(":gm_thr_id"_L1, gmThrIdValue);
        sibQ.exec();
    }

    QSqlQuery idQ(database);
    idQ.prepare("SELECT id FROM messages WHERE account_email=:account_email AND logical_key=:logical_key LIMIT 1"_L1);
    idQ.bindValue(":account_email"_L1, accountEmail);
    idQ.bindValue(":logical_key"_L1, lkey);
    if (!idQ.exec() || !idQ.next()) {
        return;
    }
    const int messageId = idQ.value(0).toInt();

    const bool isCategoryFolder = folderValue.contains("/Categories/"_L1, Qt::CaseInsensitive);
    if (!isCategoryFolder) {
        if (upsertFolderEdge(database, accountEmail, messageId, folderValue, uidValue, unreadValue)) {
            incrementNewMessageCount(folderValue);
            // For INBOX messages, also increment the inferred Gmail category count
            // so category tabs show +X immediately.
            if (!rawGmailLabels.isEmpty()) {
                const QString catFolder = inferCategoryFromLabels(rawGmailLabels);
                if (!catFolder.isEmpty())
                    incrementNewMessageCount(catFolder);
            }
            // Desktop notification for new unread inbox messages.
            if (m_desktopNotifyEnabled.load()
                && unreadValue
                && folderValue.compare("INBOX"_L1, Qt::CaseInsensitive) == 0) {
                QVariantMap info;
                info["senderDisplay"_L1] = senderDisplayNameValue.isEmpty() ? senderEmailValue : senderDisplayNameValue;
                info["senderRaw"_L1]     = senderValue;
                info["subject"_L1]       = subjectValue;
                info["snippet"_L1]       = snippetValue;
                info["accountEmail"_L1]  = accountEmail;
                info["folder"_L1]        = folderValue;
                info["uid"_L1]           = uidValue;
                QMetaObject::invokeMethod(this, [this, info]() {
                    emit newMailReceived(info);
                }, Qt::QueuedConnection);
            }
        }
    } else {
        // Check if this category label is new before upserting.
        QSqlQuery qCheckLabel(database);
        qCheckLabel.prepare("SELECT 1 FROM message_labels WHERE account_email=:a AND message_id=:m AND label=:l"_L1);
        qCheckLabel.bindValue(":a"_L1, accountEmail);
        qCheckLabel.bindValue(":m"_L1, messageId);
        qCheckLabel.bindValue(":l"_L1, folderValue);
        qCheckLabel.exec();
        const bool isNewLabel = !qCheckLabel.next();

        QSqlQuery qLabel(database);
        qLabel.prepare(R"(
            INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
            VALUES (:account_email, :message_id, :label, 'category-folder-sync', 100, datetime('now'))
            ON CONFLICT(account_email, message_id, label) DO UPDATE SET
              source='category-folder-sync',
              confidence=MAX(message_labels.confidence, 100),
              observed_at=datetime('now')
        )"_L1);
        qLabel.bindValue(":account_email"_L1, accountEmail);
        qLabel.bindValue(":message_id"_L1, messageId);
        qLabel.bindValue(":label"_L1, folderValue);
        qLabel.exec();

        if (isNewLabel)
            incrementNewMessageCount(folderValue);

        QSqlQuery qTag(database);
        qTag.prepare(R"(
            INSERT INTO tags (name, normalized_name, origin, updated_at)
            VALUES (:name, lower(:name), 'server', datetime('now'))
            ON CONFLICT(normalized_name) DO UPDATE SET
              updated_at=datetime('now')
        )"_L1);
        qTag.bindValue(":name"_L1, folderValue);
        qTag.exec();

        QSqlQuery qTagMap(database);
        qTagMap.prepare(R"(
            INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
            SELECT :account_email, :message_id, id, 'server', datetime('now')
            FROM tags
            WHERE normalized_name=lower(:label)
            ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
              observed_at=datetime('now')
        )"_L1);
        qTagMap.bindValue(":account_email"_L1, accountEmail);
        qTagMap.bindValue(":message_id"_L1, messageId);
        qTagMap.bindValue(":label"_L1, folderValue);
        qTagMap.exec();
    }

    // eM-inspired participant rows: per-message sender/recipient evidence.
    {
        QSqlQuery qDel(database);
        qDel.prepare("DELETE FROM message_participants WHERE account_email=:account_email AND message_id=:message_id"_L1);
        qDel.bindValue(":account_email"_L1, accountEmail);
        qDel.bindValue(":message_id"_L1, messageId);
        qDel.exec();

        auto insertParticipant = [&](const QString &role,
                                     int position,
                                     const QString &displayName,
                                     const QString &address,
                                     const QString &source) {
            if (address.trimmed().isEmpty() && displayName.trimmed().isEmpty()) return;
            QSqlQuery qP(database);
            qP.prepare(R"(
                INSERT INTO message_participants (account_email, message_id, role, position, display_name, address, source)
                VALUES (:account_email, :message_id, :role, :position, :display_name, :address, :source)
                ON CONFLICT(account_email, message_id, role, position) DO UPDATE SET
                  display_name=excluded.display_name,
                  address=excluded.address,
                  source=excluded.source
            )"_L1);
            qP.bindValue(":account_email"_L1, accountEmail);
            qP.bindValue(":message_id"_L1, messageId);
            qP.bindValue(":role"_L1, role);
            qP.bindValue(":position"_L1, position);
            qP.bindValue(":display_name"_L1, displayName.trimmed());
            qP.bindValue(":address"_L1, Kestrel::normalizeEmail(address));
            qP.bindValue(":source"_L1, source);
            qP.exec();
        };

        insertParticipant("sender"_L1, 0, senderDisplayNameValue, senderEmailValue, "header"_L1);
        insertParticipant("recipient"_L1, 0, recipientDisplayNameValue, recipientEmailValue, "header"_L1);
    }

    // Identity-level avatar cache store: one avatar file URL per normalized email.
    // Data URIs are written to disk; file:// URLs are stored in the DB.
    if (!senderEmailValue.isEmpty() && !avatarUrlValue.isEmpty()) {
        const QString storedSenderUrl = avatarUrlValue;
        if (!storedSenderUrl.isEmpty()) {
            QSqlQuery qAvatar(database);
            qAvatar.prepare(R"(
                INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
                VALUES (:email, :avatar_url, :source, datetime('now'), 0)
                ON CONFLICT(email) DO UPDATE SET
                  avatar_url=excluded.avatar_url,
                  source=CASE WHEN excluded.source IS NOT NULL AND length(trim(excluded.source))>0 THEN excluded.source ELSE contact_avatars.source END,
                  last_checked_at=datetime('now'),
                  failure_count=0
            )"_L1);
            qAvatar.bindValue(":email"_L1, senderEmailValue);
            qAvatar.bindValue(":avatar_url"_L1, storedSenderUrl);
            qAvatar.bindValue(":source"_L1, avatarSourceValue);
            qAvatar.exec();
            { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(senderEmailValue, storedSenderUrl); }
        }
    }
    if (!recipientEmailValue.isEmpty() && !recipientAvatarUrlValue.isEmpty()) {
        const QString storedRecipientUrl = recipientAvatarUrlValue;
        if (!storedRecipientUrl.isEmpty()) {
            QSqlQuery qAvatar(database);
            qAvatar.prepare(R"(
                INSERT INTO contact_avatars (email, avatar_url, source, last_checked_at, failure_count)
                VALUES (:email, :avatar_url, 'google-people', datetime('now'), 0)
                ON CONFLICT(email) DO UPDATE SET
                  avatar_url=excluded.avatar_url,
                  source='google-people',
                  last_checked_at=datetime('now'),
                  failure_count=0
            )"_L1);
            qAvatar.bindValue(":email"_L1, recipientEmailValue);
            qAvatar.bindValue(":avatar_url"_L1, storedRecipientUrl);
            qAvatar.exec();
            { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.insert(recipientEmailValue, storedRecipientUrl); }
        }
    } else if (!recipientEmailValue.isEmpty() && recipientAvatarLookupMiss) {
        QSqlQuery qAvatarMiss(database);
        qAvatarMiss.prepare(R"(
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
        qAvatarMiss.bindValue(":email"_L1, recipientEmailValue);
        qAvatarMiss.exec();
        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(recipientEmailValue); }  // CASE expression; evict to re-query
    }

    if (!senderEmailValue.isEmpty() && avatarUrlValue.isEmpty()) {
        // No avatar resolved for this sender — record a miss so we don't retry too soon.
        // Never store raw favicon URLs as fallback (they 404 without session cookies).
        QSqlQuery qAvatarMiss(database);
        qAvatarMiss.prepare(R"(
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
        )"_L1);
        qAvatarMiss.bindValue(":email"_L1, senderEmailValue);
        qAvatarMiss.exec();
        { QMutexLocker lock(&m_avatarCacheMutex); m_avatarCache.remove(senderEmailValue); }  // CASE expression; evict to re-query
    }

    auto upsertDisplayName = [&](const QString &email, const QString &displayName, const QString &source) {
        const QString e = Kestrel::normalizeEmail(email);
        const QString cand = displayName.trimmed();
        if (e.isEmpty() || cand.isEmpty()) return;

        QString existing;
        int existingScore = std::numeric_limits<int>::min() / 4;
        {
            QSqlQuery qCur(database);
            qCur.prepare("SELECT display_name, display_score FROM contact_display_names WHERE email=:email LIMIT 1"_L1);
            qCur.bindValue(":email"_L1, e);
            if (qCur.exec() && qCur.next()) {
                existing = qCur.value(0).toString().trimmed();
                existingScore = qCur.value(1).toInt();
            }
        }

        const int newScore = displayNameScoreForEmail(cand, e);
        const int oldScore = !existing.isEmpty() ? existingScore : (std::numeric_limits<int>::min() / 4);
        if (!existing.isEmpty() && newScore < oldScore) return;

        QSqlQuery qName(database);
        qName.prepare(R"(
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
        qName.bindValue(":email"_L1, e);
        qName.bindValue(":display_name"_L1, cand);
        qName.bindValue(":source"_L1, source);
        qName.bindValue(":display_score"_L1, newScore);
        qName.exec();
    };

    upsertDisplayName(senderEmailValue, senderDisplayNameValue, "sender-header"_L1);
    if (!recipientEmailValue.isEmpty()
            && recipientEmailValue.compare(accountEmail.trimmed(), Qt::CaseInsensitive) != 0) {
        upsertDisplayName(recipientEmailValue, recipientDisplayNameValue, "recipient-header"_L1);
    }

    // Migration scaffold write path: persist observed Gmail labels with provenance.
    if (!rawGmailLabels.trimmed().isEmpty()) {
        static const QRegularExpression tokenRe("\"([^\"]+)\"|([^\\s()]+)"_L1);
        QRegularExpressionMatchIterator it = tokenRe.globalMatch(rawGmailLabels);
        while (it.hasNext()) {
            const auto m = it.next();
            QString label = m.captured(1).trimmed();
            if (label.isEmpty()) label = m.captured(2).trimmed();
            if (label.isEmpty()) continue;

            QSqlQuery qLabel(database);
            qLabel.prepare(R"(
                INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
                VALUES (:account_email, :message_id, :label, 'imap-label', 100, datetime('now'))
                ON CONFLICT(account_email, message_id, label) DO UPDATE SET
                  source='imap-label',
                  confidence=MAX(message_labels.confidence, 100),
                  observed_at=datetime('now')
            )"_L1);
            qLabel.bindValue(":account_email"_L1, accountEmail);
            qLabel.bindValue(":message_id"_L1, messageId);
            qLabel.bindValue(":label"_L1, label);
            qLabel.exec();

            // Unified tag projection (global tags + per-message mapping).
            QSqlQuery qTag(database);
            qTag.prepare(R"(
                INSERT INTO tags (name, normalized_name, origin, updated_at)
                VALUES (:name, lower(:name), 'server', datetime('now'))
                ON CONFLICT(normalized_name) DO UPDATE SET
                  updated_at=datetime('now')
            )"_L1);
            qTag.bindValue(":name"_L1, label);
            qTag.exec();

            QSqlQuery qTagMap(database);
            qTagMap.prepare(R"(
                INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
                SELECT :account_email, :message_id, id, 'server', datetime('now')
                FROM tags
                WHERE normalized_name=lower(:label)
                ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
                  observed_at=datetime('now')
            )"_L1);
            qTagMap.bindValue(":account_email"_L1, accountEmail);
            qTagMap.bindValue(":message_id"_L1, messageId);
            qTagMap.bindValue(":label"_L1, label);
            qTagMap.exec();
        }
    }

    // Protect direct Primary evidence: if Gmail labels explicitly reported Primary,
    // ensure a Primary label row exists (not a folder edge - categories are labels only).
    if (primaryLabelObserved) {
        const QString primaryLabel = "[Gmail]/Categories/Primary"_L1;

        QSqlQuery qCheckPrimary(database);
        qCheckPrimary.prepare("SELECT 1 FROM message_labels WHERE account_email=:a AND message_id=:m AND label=:l"_L1);
        qCheckPrimary.bindValue(":a"_L1, accountEmail);
        qCheckPrimary.bindValue(":m"_L1, messageId);
        qCheckPrimary.bindValue(":l"_L1, primaryLabel);
        qCheckPrimary.exec();
        const bool isNewPrimary = !qCheckPrimary.next();

        QSqlQuery qLabel(database);
        qLabel.prepare(R"(
            INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
            VALUES (:account_email, :message_id, :label, 'x-gm-labels-primary', 100, datetime('now'))
            ON CONFLICT(account_email, message_id, label) DO UPDATE SET
              source='x-gm-labels-primary',
              confidence=MAX(message_labels.confidence, 100),
              observed_at=datetime('now')
        )"_L1);
        qLabel.bindValue(":account_email"_L1, accountEmail);
        qLabel.bindValue(":message_id"_L1, messageId);
        qLabel.bindValue(":label"_L1, primaryLabel);
        qLabel.exec();

        if (isNewPrimary)
            incrementNewMessageCount(primaryLabel);

        QSqlQuery qTag(database);
        qTag.prepare(R"(
            INSERT INTO tags (name, normalized_name, origin, updated_at)
            VALUES (:name, lower(:name), 'server', datetime('now'))
            ON CONFLICT(normalized_name) DO UPDATE SET
              updated_at=datetime('now')
        )"_L1);
        qTag.bindValue(":name"_L1, primaryLabel);
        qTag.exec();

        QSqlQuery qTagMap(database);
        qTagMap.prepare(R"(
            INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
            SELECT :account_email, :message_id, id, 'server', datetime('now')
            FROM tags
            WHERE normalized_name=lower(:label)
            ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET
              observed_at=datetime('now')
        )"_L1);
        qTagMap.bindValue(":account_email"_L1, accountEmail);
        qTagMap.bindValue(":message_id"_L1, messageId);
        qTagMap.bindValue(":label"_L1, primaryLabel);
        qTagMap.exec();
    }


    // Attachment metadata from BODYSTRUCTURE — stored on first sync pass.
    {
        const QVariantList attachments = header.value("attachments"_L1).toList();
        if (!attachments.isEmpty())
            upsertAttachments(messageId, accountEmail, attachments);
    }

    // Server truth model: when a message is observed in Trash, remove stale membership
    // from non-trash folders for this account/message. It can be re-added later only if
    // the server reports it back in those folders.
    if (isTrashFolderName(folderValue)) {
        QList<QPair<QString, QString>> edgesToDelete;
        QSqlQuery qFindEdges(database);
        qFindEdges.prepare(R"(
            SELECT folder, uid
            FROM message_folder_map
            WHERE account_email=:account_email
              AND message_id=:message_id
              AND lower(folder) NOT IN ('trash','[gmail]/trash','[google mail]/trash')
              AND lower(folder) NOT LIKE '%/trash'
        )"_L1);
        qFindEdges.bindValue(":account_email"_L1, accountEmail);
        qFindEdges.bindValue(":message_id"_L1, messageId);
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
    return { "Primary"_L1, "Promotions"_L1, "Social"_L1 };
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
    q.prepare(R"(
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
    )"_L1);
    if (q.exec()) {
        while (q.next()) {
            const QString label = q.value(0).toString().trimmed();
            if (isSystemLabelName(label) || isCategoryFolderName(label)) continue;

            QVariantMap row;
            row.insert("label"_L1, label);
            row.insert("name"_L1, q.value(1).toString().trimmed());
            row.insert("total"_L1, q.value(2).toInt());
            row.insert("unread"_L1, q.value(3).toInt());
            row.insert("color"_L1, q.value(4).toString().trimmed());
            byLabel.insert(label, row);
        }
    }

    // Add Important as a tag (folder-backed, not label-backed)
    QSqlQuery qImportant(database);
    qImportant.prepare(R"(
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
    )"_L1);
    if (qImportant.exec() && qImportant.next()) {
        const int total = qImportant.value(0).toInt();
        QVariantMap row;
        row.insert("label"_L1, "important"_L1);
        row.insert("name"_L1, "Important"_L1);
        row.insert("total"_L1, total);
        row.insert("unread"_L1, qImportant.value(1).toInt());
        row.insert("color"_L1, QString());
        byLabel.insert("important"_L1, row);
    } else {
        QVariantMap row;
        row.insert("label"_L1, "important"_L1);
        row.insert("name"_L1, "Important"_L1);
        row.insert("total"_L1, 0);
        row.insert("unread"_L1, 0);
        row.insert("color"_L1, QString());
        byLabel.insert("important"_L1, row);
    }

    // Fallback/union with top-level custom folders so Tags section isn't empty when
    // labels table is sparse.
    QSqlQuery qFolders(database);
    qFolders.prepare("SELECT lower(name), lower(flags) FROM folders"_L1);
    if (qFolders.exec()) {
        while (qFolders.next()) {
            const QString name = qFolders.value(0).toString().trimmed();
            if (name.isEmpty()) continue;
            if (name == "[google mail]"_L1) continue;
            if (name.contains('/')) continue;

            if (isSystemLabelName(name) || isCategoryFolderName(name)) continue;
            if (byLabel.contains(name)) continue;

            QVariantMap row;
            row.insert("label"_L1, name);
            row.insert("name"_L1, name);
            row.insert("total"_L1, 0);
            row.insert("unread"_L1, 0);
            byLabel.insert(name, row);
        }
    }

    QStringList keys = byLabel.keys();
    std::sort(keys.begin(), keys.end());

    for (int i = 0; i < keys.size(); ++i) {
        const QString &k = keys.at(i);
        QVariantMap row = byLabel.value(k);

        QString color = row.value("color"_L1).toString().trimmed();
        if (color.isEmpty()) {
            // Deterministic high-separation hues using golden-angle spacing.
            const int hue = (i * 137) % 360;
            QColor c;
            c.setHsv(hue, 190, 225);
            color = c.name(QColor::HexRgb);

            QSqlQuery qColor(database);
            qColor.prepare("UPDATE tags SET color=:color, updated_at=datetime('now') WHERE normalized_name=:name"_L1);
            qColor.bindValue(":color"_L1, color);
            qColor.bindValue(":name"_L1, k);
            qColor.exec();
        }

        row.insert("color"_L1, color);
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
    q.prepare(R"(
        INSERT INTO folders (account_email, name, flags, special_use)
        VALUES (:account_email, :name, :flags, :special_use)
        ON CONFLICT(account_email, name) DO UPDATE SET
          flags = excluded.flags,
          special_use = excluded.special_use
    )"_L1);

    q.bindValue(":account_email"_L1, folder.value("accountEmail"_L1));
    q.bindValue(":name"_L1, folder.value("name"_L1));
    q.bindValue(":flags"_L1, folder.value("flags"_L1));
    q.bindValue(":special_use"_L1, folder.value("specialUse"_L1));
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
    const bool isCategoryFolder = folder.contains("/Categories/"_L1, Qt::CaseInsensitive);
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
    qOrphan.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);
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
    qFind.prepare(R"(
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
    )"_L1.arg(placeholders.join(","_L1)));
    qFind.bindValue(":account_email"_L1, acc);
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

    // Check if this is a genuinely new edge.
    QSqlQuery qCheck(database);
    qCheck.prepare("SELECT 1 FROM message_folder_map WHERE account_email=:a AND folder=:f AND uid=:u"_L1);
    qCheck.bindValue(":a"_L1, accountEmail);
    qCheck.bindValue(":f"_L1, folder);
    qCheck.bindValue(":u"_L1, uid);
    qCheck.exec();
    const bool isNew = !qCheck.next();

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

    if (isNew) {
        incrementNewMessageCount(folder);
        // Desktop notification for new unread inbox messages.
        if (m_desktopNotifyEnabled.load()
            && unread
            && folder.compare("INBOX"_L1, Qt::CaseInsensitive) == 0) {
            QSqlQuery qMsg(database);
            qMsg.prepare("SELECT sender, subject, snippet FROM messages WHERE id=:id LIMIT 1"_L1);
            qMsg.bindValue(":id"_L1, messageId);
            if (qMsg.exec() && qMsg.next()) {
                const QString rawSender = qMsg.value(0).toString().trimmed();
                const QString se = extractFirstEmail(rawSender);
                const QString sn = extractExplicitDisplayName(rawSender, se);
                QVariantMap info;
                info["senderDisplay"_L1] = sn.isEmpty() ? se : sn;
                info["senderRaw"_L1]     = rawSender;
                info["subject"_L1]       = qMsg.value(1).toString();
                info["snippet"_L1]       = qMsg.value(2).toString();
                info["accountEmail"_L1]  = accountEmail;
                info["folder"_L1]        = folder;
                info["uid"_L1]           = uid;
                QMetaObject::invokeMethod(this, [this, info]() {
                    emit newMailReceived(info);
                }, Qt::QueuedConnection);
            }
        }
    }
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
    q.prepare(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email "
        "WHERE mfm.account_email=:account_email AND lower(mfm.folder)=lower(:folder)"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
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
    q.prepare(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id "
        "WHERE mfm.account_email = :account_email AND lower(mfm.folder) = lower(:folder) "
        "AND (m.snippet IS NULL OR m.snippet = '')"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
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
    q.prepare("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
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
    q.prepare(R"(
        SELECT uid_next, highest_modseq, messages
        FROM folder_sync_status
        WHERE account_email=:account_email
          AND lower(folder)=lower(:folder)
        LIMIT 1
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec() || !q.next())
        return out;

    out.insert("uidNext"_L1, q.value(0).toLongLong());
    out.insert("highestModSeq"_L1, q.value(1).toLongLong());
    out.insert("messages"_L1, q.value(2).toLongLong());
    return out;
}

void DataStore::upsertFolderSyncStatus(const QString &accountEmail, const QString &folder,
                                       const qint64 uidNext, const qint64 highestModSeq, const qint64 messages)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return;

    QSqlQuery q(database);
    q.prepare(R"(
        INSERT INTO folder_sync_status(account_email, folder, uid_next, highest_modseq, messages, updated_at)
        VALUES(:account_email, :folder, :uid_next, :highest_modseq, :messages, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          uid_next=excluded.uid_next,
          highest_modseq=excluded.highest_modseq,
          messages=excluded.messages,
          updated_at=datetime('now')
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":uid_next"_L1, uidNext);
    q.bindValue(":highest_modseq"_L1, highestModSeq);
    q.bindValue(":messages"_L1, messages);
    q.exec();
}

qint64 DataStore::folderLastSyncModSeq(const QString &accountEmail, const QString &folder) const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return 0;

    QSqlQuery q(database);
    q.prepare(
        "SELECT last_sync_modseq FROM folder_sync_status "
        "WHERE account_email=:account_email AND lower(folder)=lower(:folder) LIMIT 1"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
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
    q.prepare(R"(
        INSERT INTO folder_sync_status(account_email, folder, last_sync_modseq, updated_at)
        VALUES(:account_email, :folder, :modseq, datetime('now'))
        ON CONFLICT(account_email, folder) DO UPDATE SET
          last_sync_modseq=excluded.last_sync_modseq,
          updated_at=datetime('now')
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":modseq"_L1, modseq);
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
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":limit"_L1, boundedLimit);

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
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());

    if (!q.exec())
        return out;

    QSet<qint64> seenMessageIds;
    while (q.next()) {
        const qint64 messageId = q.value(0).toLongLong();
        if (seenMessageIds.contains(messageId))
            continue;
        seenMessageIds.insert(messageId);

        QVariantMap row;
        row.insert("messageId"_L1, messageId);
        row.insert("folder"_L1, q.value(1).toString());
        row.insert("uid"_L1, q.value(2).toString());
        out.push_back(row);

        if (out.size() >= boundedLimit)
            break;
    }

    if (!out.isEmpty())
        return out;

    // Fallback if received_at parsing/filtering excludes everything: keep Inbox-first
    // ordering and dedupe by message id, but without time window.
    QSqlQuery qFallback(database);
    qFallback.prepare(R"(
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
    )"_L1);
    qFallback.bindValue(":account_email"_L1, accountEmail.trimmed());

    if (!qFallback.exec())
        return out;

    seenMessageIds.clear();
    while (qFallback.next()) {
        const qint64 messageId = qFallback.value(0).toLongLong();
        if (seenMessageIds.contains(messageId))
            continue;
        seenMessageIds.insert(messageId);

        QVariantMap row;
        row.insert("messageId"_L1, messageId);
        row.insert("folder"_L1, qFallback.value(1).toString());
        row.insert("uid"_L1, qFallback.value(2).toString());
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
    qMid.prepare(R"(
        SELECT message_id
        FROM message_folder_map
        WHERE account_email=:account_email
          AND lower(folder)=lower(:folder)
          AND uid=:uid
        LIMIT 1
    )"_L1);
    qMid.bindValue(":account_email"_L1, accountEmail.trimmed());
    qMid.bindValue(":folder"_L1, folder.trimmed());
    qMid.bindValue(":uid"_L1, uid.trimmed());
    if (!qMid.exec() || !qMid.next()) {
        QVariantMap fallback;
        fallback.insert("folder"_L1, folder.trimmed());
        fallback.insert("uid"_L1, uid.trimmed());
        out.push_back(fallback);
        return out;
    }

    const qint64 messageId = qMid.value(0).toLongLong();
    QSqlQuery q(database);
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":message_id"_L1, messageId);
    q.bindValue(":requested_folder"_L1, folder.trimmed());
    if (!q.exec()) return out;
    while (q.next()) {
        const QString f = q.value(0).toString().trimmed();
        const QString u = q.value(1).toString().trimmed();
        if (f.isEmpty() || u.isEmpty()) continue;
        QVariantMap row;
        row.insert("folder"_L1, f);
        row.insert("uid"_L1, u);
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
    q.prepare(R"(
        SELECT m.body_html
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id
        WHERE mfm.account_email=:account_email
          AND lower(mfm.folder)=lower(:folder)
          AND mfm.uid=:uid
        LIMIT 1
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":uid"_L1, uid.trimmed());

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
    const int tableOpen  = static_cast<int>(lower.count("<table"_L1));
    const int tableClose = static_cast<int>(lower.count("</table>"_L1));
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
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":uid"_L1, uid.trimmed());
    if (!q.exec() || !q.next()) return row;

    row.insert("accountEmail"_L1, q.value(0));
    row.insert("folder"_L1, q.value(1));
    row.insert("uid"_L1, q.value(2));
    row.insert("messageId"_L1, q.value(3));
    row.insert("sender"_L1, q.value(4));
    row.insert("subject"_L1, q.value(5));
    row.insert("recipient"_L1, q.value(6));
    row.insert("recipientAvatarUrl"_L1, q.value(7));
    row.insert("receivedAt"_L1, q.value(8));
    row.insert("snippet"_L1, q.value(9));
    row.insert("bodyHtml"_L1, q.value(10));
    row.insert("avatarDomain"_L1, q.value(11));
    row.insert("avatarUrl"_L1, q.value(12));
    row.insert("avatarSource"_L1, q.value(13));
    row.insert("unread"_L1,          q.value(14).toInt() == 1);
    row.insert("listUnsubscribe"_L1, q.value(15).toString());
    row.insert("replyTo"_L1,         q.value(16).toString());
    row.insert("returnPath"_L1,      q.value(17).toString());
    row.insert("authResults"_L1,     q.value(18).toString());
    row.insert("xMailer"_L1,         q.value(19).toString());
    row.insert("inReplyTo"_L1,       q.value(20).toString());
    row.insert("references"_L1,      q.value(21).toString());
    row.insert("espVendor"_L1,       q.value(22).toString());
    row.insert("threadId"_L1,        q.value(23).toString());
    row.insert("threadCount"_L1,     q.value(24).toInt());
    row.insert("cc"_L1,              q.value(25).toString());
    row.insert("flagged"_L1,         q.value(26).toInt() == 1);
    return row;
}

QVariantList DataStore::messagesForThread(const QString &accountEmail, const QString &threadId) const
{
    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen() || threadId.trimmed().isEmpty())
        return {};

    // One row per logical message (GROUP BY cm.id), ordered oldest-first.
    QSqlQuery q(database);
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":thread_id"_L1,     threadId.trimmed());
    if (!q.exec()) return {};

    QVariantList result;
    QSet<int> seen;
    while (q.next()) {
        const int msgId = q.value(3).toInt();
        if (seen.contains(msgId)) continue;
        seen.insert(msgId);
        QVariantMap row;
        row["accountEmail"_L1] = q.value(0).toString();
        row["folder"_L1]       = q.value(1).toString();
        row["uid"_L1]          = q.value(2).toString();
        row["messageId"_L1]    = msgId;
        row["sender"_L1]       = q.value(4).toString();
        row["subject"_L1]      = q.value(5).toString();
        row["recipient"_L1]    = q.value(6).toString();
        row["receivedAt"_L1]   = q.value(7).toString();
        row["snippet"_L1]      = q.value(8).toString();
        row["bodyHtml"_L1]     = q.value(9).toString();
        row["avatarDomain"_L1] = q.value(10).toString();
        row["avatarUrl"_L1]    = q.value(11).toString();
        row["avatarSource"_L1] = q.value(12).toString();
        row["unread"_L1]       = q.value(13).toInt() == 1;
        row["threadId"_L1]     = q.value(14).toString();
        row["replyTo"_L1]      = q.value(15).toString();
        row["cc"_L1]           = q.value(16).toString();
        row["flagged"_L1]      = q.value(17).toInt() == 1;
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
    q.prepare("SELECT 1 FROM sender_image_permissions WHERE domain=:domain LIMIT 1"_L1);
    q.bindValue(":domain"_L1, domain.trimmed().toLower());
    return q.exec() && q.next();
}

void DataStore::setTrustedSenderDomain(const QString &domain)
{
    const QString d = domain.trimmed().toLower();
    if (d.isEmpty()) return;
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    QSqlQuery q(database);
    q.prepare(
        "INSERT INTO sender_image_permissions (domain) VALUES (:domain) ON CONFLICT(domain) DO NOTHING"_L1);
    q.bindValue(":domain"_L1, d);
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
        qPrev.prepare(R"(
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
        )"_L1);
        qPrev.bindValue(":account_email"_L1, accountEmail.trimmed());
        qPrev.bindValue(":folder"_L1, folder.trimmed());
        qPrev.bindValue(":uid"_L1, uid.trimmed());
        if (qPrev.exec() && qPrev.next()) {
            prevLen = qPrev.value(0).toInt();
            senderForTP = qPrev.value(1).toString();
            prevTP = qPrev.value(2).toInt();
        }
    }

    const int hasTP = computeHasTrackingPixel(html, senderForTP) ? 1 : 0;

    QSqlQuery q(database);
    q.prepare(R"(
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
    )"_L1);
    q.bindValue(":body_html"_L1, html);
    q.bindValue(":has_tp"_L1, hasTP);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    q.bindValue(":uid"_L1, uid.trimmed());
    if (!q.exec()) return false;

    const bool changed = q.numRowsAffected() > 0;

    const QString htmlLower = html.left(1024).toLower();
    const bool hasHtmlish = kReHtmlish.match(html).hasMatch();
    const bool hasMarkdownLinks = kReMarkdownLinks.match(html).hasMatch();
    const bool hasMimeHeaders = htmlLower.contains("content-type:"_L1)
                             || htmlLower.contains("mime-version:"_L1);
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
    keys << "favorites:all-inboxes"_L1
         << "favorites:unread"_L1
         << "favorites:flagged"_L1;
    for (const QVariant &fv : m_folders) {
        const QVariantMap f = fv.toMap();
        const QString rawName = f.value("name"_L1).toString().trimmed();
        if (rawName.isEmpty()) continue;
        if (rawName.contains("/Categories/"_L1, Qt::CaseInsensitive)) continue;
        // Normalize to match QML's normalizedRemoteFolderName: [Google Mail]/ → [Gmail]/,
        // but keep the [Gmail]/ prefix so the cache key matches what QML passes.
        QString norm = rawName.toLower();
        if (norm.startsWith("[google mail]/"_L1))
            norm = "[gmail]/"_L1 + norm.mid(14);
        keys << ("account:"_L1 + norm);
        if (f.value("specialUse"_L1).toString().trimmed().isEmpty()
                && !norm.contains(QLatin1Char('/')))
            keys << ("tag:"_L1 + norm);
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
            const QString mid = v.toMap().value("messageId"_L1).toString();
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
            qImp.prepare("SELECT DISTINCT message_id FROM message_folder_map WHERE lower(folder) LIKE '%/important' AND message_id IN (%1)"_L1.arg(inClause));
            for (int i = 0; i < messageIds.size(); ++i)
                qImp.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qImp.exec()) {
                while (qImp.next()) importantMessageIds.insert(qImp.value(0).toString());
            }
            // Locally toggled
            QSqlQuery qImpL(database);
            qImpL.prepare("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)='important' AND message_id IN (%1)"_L1.arg(inClause));
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
            q.prepare("SELECT DISTINCT message_id FROM message_attachments WHERE message_id IN (%1)"_L1.arg(placeholders.join(',')));
            for (int i = 0; i < messageIds.size(); ++i)
                q.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));

            if (q.exec()) {
                while (q.next())
                    withAttachments.insert(q.value(0).toString());
            }
        }

        for (int i = 0; i < list.size(); ++i) {
            QVariantMap row = list.at(i).toMap();
            const QString mid = row.value("messageId"_L1).toString();
            row.insert("hasAttachments"_L1,    withAttachments.contains(mid));
            row.insert("isImportant"_L1,       importantMessageIds.contains(mid));
            row.insert("hasTrackingPixel"_L1,  row.value("hasTrackingPixel"_L1).toBool());
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
        const int total = static_cast<int>(in.size());
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

    if (key.startsWith("local:"_L1, Qt::CaseInsensitive)) {
        if (hasMore) *hasMore = false;
        return {};
    }

    if (key.compare("favorites:flagged"_L1, Qt::CaseInsensitive) == 0) {
        if (!database.isValid() || !database.isOpen()) return {};
        const int safeOffset = qMax(0, offset);
        QSqlQuery qFlagged(database);
        qFlagged.prepare(R"(
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
        )"_L1);
        qFlagged.bindValue(":limit"_L1, limit > 0 ? limit + 1 : 5000);
        qFlagged.bindValue(":offset"_L1, safeOffset);

        QVariantList out;
        if (qFlagged.exec()) {
            while (qFlagged.next()) {
                const QString tid  = qFlagged.value(15).toString().trimmed();
                QString gtid       = qFlagged.value(17).toString().trimmed();
                const QString acct = qFlagged.value(0).toString();
                const QString mid  = qFlagged.value(3).toString();
                if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive))
                    gtid = tid.mid(3).trimmed();

                QVariantMap row;
                row.insert("accountEmail"_L1, qFlagged.value(0));
                row.insert("folder"_L1, qFlagged.value(1));
                row.insert("uid"_L1, qFlagged.value(2));
                row.insert("messageId"_L1, mid);
                row.insert("sender"_L1, qFlagged.value(4));
                row.insert("subject"_L1, qFlagged.value(5));
                row.insert("recipient"_L1, qFlagged.value(6));
                row.insert("recipientAvatarUrl"_L1, qFlagged.value(7));
                row.insert("receivedAt"_L1, qFlagged.value(8));
                row.insert("snippet"_L1,          qFlagged.value(9));
                row.insert("hasTrackingPixel"_L1, qFlagged.value(10).toInt() == 1);
                row.insert("avatarDomain"_L1,     qFlagged.value(11));
                row.insert("avatarUrl"_L1, qFlagged.value(12));
                row.insert("avatarSource"_L1, qFlagged.value(13));
                row.insert("unread"_L1, qFlagged.value(14).toInt() == 1);
                row.insert("threadId"_L1,    tid);
                row.insert("threadCount"_L1, qFlagged.value(16).toInt());
                row.insert("gmThrId"_L1,     gtid);
                row.insert("allSenders"_L1,  qFlagged.value(18));
                row.insert("flagged"_L1,     true);
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

    if (key.compare("favorites:all-inboxes"_L1, Qt::CaseInsensitive) == 0
        || key.compare("favorites:unread"_L1, Qt::CaseInsensitive) == 0) {
        QVariantList out;
        if (database.isValid() && database.isOpen()) {
            const bool unreadOnly = key.compare("favorites:unread"_L1, Qt::CaseInsensitive) == 0;
            const int safeOffset = qMax(0, offset);
            const int chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
            const int targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

            QSet<QString> seenKeys;
            int rawOffset = 0;
            bool exhausted = false;
            while (!exhausted) {
                QSqlQuery q(database);
                q.prepare(R"(
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
                )"_L1);
                q.bindValue(":unread_only"_L1, unreadOnly ? 1 : 0);
                q.bindValue(":limit"_L1, chunkSize);
                q.bindValue(":offset"_L1, rawOffset);

                int fetchedRaw = 0;
                if (q.exec()) {
                    while (q.next()) {
                        ++fetchedRaw;

                        const QString tid   = q.value(15).toString().trimmed();
                        QString gtid        = q.value(17).toString().trimmed();
                        const QString acct  = q.value(0).toString();
                        const QString mid   = q.value(3).toString();
                        if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive))
                            gtid = tid.mid(3).trimmed();
                        const QString tkey  = !gtid.isEmpty()
                            ? acct + "|gtid:"_L1 + gtid
                            : (tid.isEmpty()
                                ? acct + "|msg:"_L1 + mid
                                : acct + "|tid:"_L1 + tid);
                        if (seenKeys.contains(tkey)) continue;
                        seenKeys.insert(tkey);

                        QVariantMap row;
                        row.insert("accountEmail"_L1, q.value(0));
                        row.insert("folder"_L1, q.value(1));
                        row.insert("uid"_L1, q.value(2));
                        row.insert("messageId"_L1, mid);
                        row.insert("sender"_L1, q.value(4));
                        row.insert("subject"_L1, q.value(5));
                        row.insert("recipient"_L1, q.value(6));
                        row.insert("recipientAvatarUrl"_L1, q.value(7));
                        row.insert("receivedAt"_L1, q.value(8));
                        row.insert("snippet"_L1,          q.value(9));
                        row.insert("hasTrackingPixel"_L1, q.value(10).toInt() == 1);
                        row.insert("avatarDomain"_L1,     q.value(11));
                        row.insert("avatarUrl"_L1, q.value(12));
                        row.insert("avatarSource"_L1, q.value(13));
                        row.insert("unread"_L1, q.value(14).toInt() == 1);
                        row.insert("threadId"_L1,    tid);
                        row.insert("threadCount"_L1, q.value(16).toInt());
                        row.insert("gmThrId"_L1,     gtid);
                        row.insert("allSenders"_L1,  q.value(18));
                        row.insert("flagged"_L1,     q.value(19).toInt() == 1);
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
                const int end = static_cast<int>(qMin(out.size(), static_cast<qsizetype>(safeOffset + limit)));
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
    if (key.startsWith("account:"_L1, Qt::CaseInsensitive)) {
        selectedFolder = key.mid("account:"_L1.size()).toLower();
    } else if (key.startsWith("tag:"_L1, Qt::CaseInsensitive)) {
        selectedTag = key.mid("tag:"_L1.size()).toLower();
    }

    // Tag selections should resolve to folder-backed queries (DB source of truth),
    // not in-memory inbox snapshots.
    if (!selectedTag.isEmpty()) {
        selectedFolder = selectedTag;
        selectedTag.clear();
    }

    const bool categoryView = (selectedFolder == "inbox"_L1
                               && !selectedCategories.isEmpty()
                               && selectedCategoryIndex >= 0
                               && selectedCategoryIndex < selectedCategories.size());

    if (!selectedFolder.isEmpty() && !categoryView) {
        const bool selectedIsTrash = isTrashFolderName(selectedFolder);
        const bool selectedIsImportantPseudo = (selectedFolder == "important"_L1);

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
                    qFolder.prepare(R"(
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
                    )"_L1);
                } else {
                    qFolder.prepare(R"(
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
                    )"_L1);
                }
                qFolder.bindValue(":folder"_L1, selectedFolder);
                qFolder.bindValue(":is_important"_L1, selectedIsImportantPseudo ? 1 : 0);
                qFolder.bindValue(":limit"_L1, chunkSize);
                qFolder.bindValue(":offset"_L1, rawOffset);

                int fetchedRaw = 0;
                if (qFolder.exec()) {
                    while (qFolder.next()) {
                        ++fetchedRaw;

                        const QString tid   = qFolder.value(15).toString().trimmed();
                        QString gtid        = qFolder.value(17).toString().trimmed();
                        const QString acct  = qFolder.value(0).toString();
                        const QString mid   = qFolder.value(3).toString();
                        if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive))
                            gtid = tid.mid(3).trimmed();
                        const QString tkey  = !gtid.isEmpty()
                            ? acct + "|gtid:"_L1 + gtid
                            : (tid.isEmpty()
                                ? acct + "|msg:"_L1 + mid
                                : acct + "|tid:"_L1 + tid);
                        if (seenFolderTids.contains(tkey)) continue;
                        seenFolderTids.insert(tkey);

                        QVariantMap row;
                        row.insert("accountEmail"_L1, qFolder.value(0));
                        row.insert("folder"_L1, qFolder.value(1));
                        row.insert("uid"_L1, qFolder.value(2));
                        row.insert("messageId"_L1, mid);
                        row.insert("sender"_L1, qFolder.value(4));
                        row.insert("subject"_L1, qFolder.value(5));
                        row.insert("recipient"_L1, qFolder.value(6));
                        row.insert("recipientAvatarUrl"_L1, qFolder.value(7));
                        row.insert("receivedAt"_L1, qFolder.value(8));
                        row.insert("snippet"_L1,          qFolder.value(9));
                        row.insert("hasTrackingPixel"_L1, qFolder.value(10).toInt() == 1);
                        row.insert("avatarDomain"_L1,     qFolder.value(11));
                        row.insert("avatarUrl"_L1, qFolder.value(12));
                        row.insert("avatarSource"_L1, qFolder.value(13));
                        row.insert("unread"_L1, qFolder.value(14).toInt() == 1);
                        row.insert("threadId"_L1,    tid);
                        row.insert("threadCount"_L1, qFolder.value(16).toInt());
                        row.insert("gmThrId"_L1,     gtid);
                        row.insert("allSenders"_L1,  qFolder.value(18));
                        row.insert("flagged"_L1,     qFolder.value(19).toInt() == 1);
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
                const int end = static_cast<int>(qMin(filtered.size(), static_cast<qsizetype>(safeOffset + limit)));
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

        auto trashExistsExpr = 
            "EXISTS (SELECT 1 FROM message_folder_map t "
            "WHERE t.account_email=m.account_email AND t.message_id=m.message_id "
            "AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash' OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash'))"_L1;

        auto labelHas = [](const QString &needle) {
            return 
                "EXISTS (SELECT 1 FROM message_labels ml "
                "WHERE ml.account_email=m.account_email AND ml.message_id=m.message_id "
                "AND lower(ml.label) LIKE '%%1%')"_L1.arg(needle);
        };

        const QString anySmartLabel = 
            "EXISTS (SELECT 1 FROM message_labels ml "
            "WHERE ml.account_email=m.account_email AND ml.message_id=m.message_id "
            "AND (lower(ml.label) LIKE '%/categories/primary%' OR lower(ml.label) LIKE '%/categories/promotion%' "
            "OR lower(ml.label) LIKE '%/categories/social%' OR lower(ml.label) LIKE '%/categories/update%' "
            "OR lower(ml.label) LIKE '%/categories/forum%' OR lower(ml.label) LIKE '%/categories/purchase%'))"_L1;
        const QString inboxMap = 
            "EXISTS (SELECT 1 FROM message_folder_map mf2 "
            "WHERE mf2.account_email=m.account_email AND mf2.message_id=m.message_id "
            "AND (lower(mf2.folder)='inbox' OR lower(mf2.folder)='[gmail]/inbox' OR lower(mf2.folder)='[google mail]/inbox' OR lower(mf2.folder) LIKE '%/inbox'))"_L1;

        QString categoryMatch;
        QString preferredFolderLike = "%/categories/primary%"_L1;
        if (cat.contains("promotion"_L1)) {
            categoryMatch = labelHas("/categories/promotion"_L1);
            preferredFolderLike = "%/categories/promotion%"_L1;
        } else if (cat.contains("social"_L1)) {
            categoryMatch = labelHas("/categories/social"_L1);
            preferredFolderLike = "%/categories/social%"_L1;
        } else if (cat.contains("purchase"_L1)) {
            categoryMatch = labelHas("/categories/purchase"_L1);
            preferredFolderLike = "%/categories/purchase%"_L1;
        } else if (cat.contains("update"_L1)) {
            categoryMatch = labelHas("/categories/update"_L1);
            preferredFolderLike = "%/categories/update%"_L1;
        } else if (cat.contains("forum"_L1)) {
            categoryMatch = labelHas("/categories/forum"_L1);
            preferredFolderLike = "%/categories/forum%"_L1;
        } else {
            categoryMatch = "(%1 OR (%2 AND NOT %3))"_L1
                    .arg(labelHas("/categories/primary"_L1),
                         inboxMap,
                         anySmartLabel);
            preferredFolderLike = "%/categories/primary%"_L1;
        }

        const QString idsSql = R"(
            SELECT m.account_email, m.message_id
            FROM message_folder_map m
            JOIN messages cm ON cm.id = m.message_id AND cm.account_email = m.account_email
            WHERE NOT %1
              AND %2
            GROUP BY m.account_email, m.message_id
            ORDER BY cm.received_at DESC
            LIMIT :limit OFFSET :offset
        )"_L1.arg(trashExistsExpr, categoryMatch);

        QSqlQuery qIds(database);
        qIds.prepare(idsSql);
        qIds.bindValue(":limit"_L1, (limit > 0 ? (limit + 1) : 5001));
        qIds.bindValue(":offset"_L1, qMax(0, offset));

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
            qRow.prepare(R"(
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
            )"_L1);

            QSet<QString> seenCatTids;
            for (const Key &k : keys) {
                qRow.bindValue(":account_email"_L1, k.account);
                qRow.bindValue(":message_id"_L1, k.messageId);
                qRow.bindValue(":preferred"_L1, preferredFolderLike);
                if (!qRow.exec() || !qRow.next()) continue;

                const QString tid  = qRow.value(15).toString().trimmed();
                QString gtid       = qRow.value(17).toString().trimmed();
                const QString acct = qRow.value(0).toString();
                const QString mid  = qRow.value(3).toString();
                if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive))
                    gtid = tid.mid(3).trimmed();
                const QString tkey = !gtid.isEmpty()
                    ? acct + "|gtid:"_L1 + gtid
                    : (tid.isEmpty()
                        ? acct + "|msg:"_L1 + mid
                        : acct + "|tid:"_L1 + tid);
                if (seenCatTids.contains(tkey)) continue;
                seenCatTids.insert(tkey);

                QVariantMap row;
                row.insert("accountEmail"_L1, qRow.value(0));
                row.insert("folder"_L1, qRow.value(1));
                row.insert("uid"_L1, qRow.value(2));
                row.insert("messageId"_L1, mid);
                row.insert("sender"_L1, qRow.value(4));
                row.insert("subject"_L1, qRow.value(5));
                row.insert("recipient"_L1, qRow.value(6));
                row.insert("recipientAvatarUrl"_L1, qRow.value(7));
                row.insert("receivedAt"_L1, qRow.value(8));
                row.insert("snippet"_L1,          qRow.value(9));
                row.insert("hasTrackingPixel"_L1, qRow.value(10).toInt() == 1);
                row.insert("avatarDomain"_L1,     qRow.value(11));
                row.insert("avatarUrl"_L1, qRow.value(12));
                row.insert("avatarSource"_L1, qRow.value(13));
                row.insert("unread"_L1, qRow.value(14).toInt() == 1);
                row.insert("threadId"_L1,    tid);
                row.insert("threadCount"_L1, qRow.value(16).toInt());
                row.insert("gmThrId"_L1,     gtid);
                row.insert("allSenders"_L1,  qRow.value(18));
                row.insert("flagged"_L1,     qRow.value(19).toInt() == 1);
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
        if (!dt.isValid()) return "older"_L1;
        const QDate target = dt.toLocalTime().date();
        const QDate today = QDate::currentDate();
        const int diffDays = static_cast<int>(target.daysTo(today));
        if (diffDays <= 0) return "today"_L1;
        if (diffDays == 1) return "yesterday"_L1;

        const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7));
        if (target >= weekStart && target < today) {
            return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
        }

        if (diffDays <= 14) return "lastWeek"_L1;
        if (diffDays <= 21) return "twoWeeksAgo"_L1;
        return "older"_L1;
    };

    auto bucketLabel = [](const QString &bucketKey) -> QString {
        if (bucketKey == "today"_L1) return "Today"_L1;
        if (bucketKey == "yesterday"_L1) return "Yesterday"_L1;
        if (bucketKey.startsWith("weekday-"_L1)) {
            bool ok = false;
            const int dow = bucketKey.mid("weekday-"_L1.size()).toInt(&ok);
            if (ok && dow >= 1 && dow <= 7) return QLocale().dayName(dow, QLocale::LongFormat);
        }
        if (bucketKey == "lastWeek"_L1) return "Last Week"_L1;
        if (bucketKey == "twoWeeksAgo"_L1) return "Two Weeks Ago"_L1;
        return "Older"_L1;
    };

    auto isExpanded = [&](const QString &bucketKey) {
        if (bucketKey == "today"_L1) return todayExpanded;
        if (bucketKey == "yesterday"_L1) return yesterdayExpanded;
        if (bucketKey.startsWith("weekday-"_L1)) return true;
        if (bucketKey == "lastWeek"_L1) return lastWeekExpanded;
        if (bucketKey == "twoWeeksAgo"_L1) return twoWeeksAgoExpanded;
        return olderExpanded;
    };

    QHash<QString, QVariantList> buckets;
    buckets.insert("today"_L1, {});
    buckets.insert("yesterday"_L1, {});
    buckets.insert("lastWeek"_L1, {});
    buckets.insert("twoWeeksAgo"_L1, {});
    buckets.insert("older"_L1, {});

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
        const QString key = bucketKeyForDate(row.value("receivedAt"_L1).toString());
        auto list = buckets.value(key);
        list.push_back(row);
        buckets.insert(key, list);
    }

    QStringList order;
    order << "today"_L1 << "yesterday"_L1;
    order << weekdayOrder;
    order << "lastWeek"_L1 << "twoWeeksAgo"_L1 << "older"_L1;
    QVariantList out;
    for (const QString &key : order) {
        const QVariantList rowsInBucket = buckets.value(key);
        if (rowsInBucket.isEmpty()) continue;

        QVariantMap header;
        header.insert("kind"_L1, "header"_L1);
        header.insert("bucketKey"_L1, key);
        header.insert("title"_L1, bucketLabel(key));
        header.insert("expanded"_L1, isExpanded(key));
        header.insert("hasTopGap"_L1, !out.isEmpty());
        out.push_back(header);

        if (isExpanded(key)) {
            for (const QVariant &rv : rowsInBucket) {
                QVariantMap msg;
                msg.insert("kind"_L1, "message"_L1);
                msg.insert("row"_L1, rv.toMap());
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
    static const QRegularExpression re(R"(^data:(image/[^;,]+);base64,(.+)$)"_L1,
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
        QCryptographicHash::hash(Kestrel::normalizeEmail(email).toUtf8(), QCryptographicHash::Sha1).toHex());
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

    const QString e = Kestrel::normalizeEmail(email);
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
        q.prepare("SELECT avatar_url FROM contact_avatars WHERE email=:email LIMIT 1"_L1);
        q.bindValue(":email"_L1, e);
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

    const QString e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) return {};

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    QSqlQuery q(database);
    q.prepare("SELECT display_name FROM contact_display_names WHERE email=:email LIMIT 1"_L1);
    q.bindValue(":email"_L1, e);
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
    q.bindValue(":pat"_L1,  pattern);
    q.bindValue(":pat2"_L1, pattern);
    q.bindValue(":lim"_L1,  limit);
    if (!q.exec())
        return out;

    while (q.next()) {
        QVariantMap row;
        row.insert("email"_L1,       q.value(0).toString());
        row.insert("displayName"_L1, q.value(1).toString().trimmed());
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

    out.insert("messages"_L1, scalar("SELECT count(*) FROM messages"_L1));
    out.insert("folderEdges"_L1, scalar("SELECT count(*) FROM message_folder_map"_L1));
    out.insert("labels"_L1, scalar("SELECT count(*) FROM message_labels"_L1));
    out.insert("tags"_L1, scalar("SELECT count(*) FROM tags"_L1));
    out.insert("tagMaps"_L1, scalar("SELECT count(*) FROM message_tag_map"_L1));

    out.insert("labelsMissingEdge"_L1, scalar(R"(
        SELECT count(*)
        FROM message_labels ml
        WHERE lower(ml.label) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_folder_map mfm
            WHERE mfm.account_email = ml.account_email
              AND mfm.message_id = ml.message_id
              AND lower(mfm.folder) = lower(ml.label)
          )
    )"_L1));

    out.insert("edgesMissingLabel"_L1, scalar(R"(
        SELECT count(*)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder) LIKE '%/categories/%'
          AND NOT EXISTS (
            SELECT 1 FROM message_labels ml
            WHERE ml.account_email = mfm.account_email
              AND ml.message_id = mfm.message_id
              AND lower(ml.label) = lower(mfm.folder)
          )
    )"_L1));

    out.insert("labelsMissingTagMap"_L1, scalar(R"(
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
    )"_L1));

    return out;
}

QString DataStore::preferredSelfDisplayName(const QString &accountEmail) const
{
    if (QThread::currentThread() != thread()) return {};

    const QString e = Kestrel::normalizeEmail(accountEmail);
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
    q.prepare("SELECT sender, recipient FROM messages WHERE lower(sender) LIKE :pat OR lower(recipient) LIKE :pat"_L1);
    q.bindValue(":pat"_L1, "%<"_L1 + e + ">%"_L1);
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

    const QString e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) return false;

    auto database = db();
    if (!database.isValid() || !database.isOpen()) return true;

    QSqlQuery q(database);
    q.prepare("SELECT avatar_url, last_checked_at, failure_count FROM contact_avatars WHERE email=:email LIMIT 1"_L1);
    q.bindValue(":email"_L1, e);
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
    if (!q.exec()) return {};

    QStringList result;
    while (q.next())
        result << Kestrel::normalizeEmail(q.value(0).toString());
    return result;
}

void DataStore::updateContactAvatar(const QString &email, const QString &avatarUrl, const QString &source)
{
    auto database = db();
    if (!database.isValid() || !database.isOpen()) return;
    const QString e = Kestrel::normalizeEmail(email);
    if (e.isEmpty()) return;

    const QString storedUrl = avatarUrl.trimmed();

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

bool DataStore::hasCachedHeadersForFolder(const QString &rawFolderName, int minCount) const
{
    const QString folder = rawFolderName.trimmed().toLower();
    if (folder.isEmpty()) return false;

    const auto database = db();
    if (!database.isValid() || !database.isOpen()) return false;

    QSqlQuery q(database);
    q.prepare(R"(
        SELECT COUNT(DISTINCT mfm.message_id)
        FROM message_folder_map mfm
        WHERE lower(mfm.folder)=:folder
    )"_L1);
    q.bindValue(":folder"_L1, folder);
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
        out.insert("total"_L1, 0);
        out.insert("unread"_L1, 0);
        return out;
    }

    auto readCountPair = [&](QSqlQuery &q) {
        if (q.exec() && q.next()) {
            total = q.value(0).toInt();
            unread = q.value(1).toInt();
        }
    };
    if (key == "favorites:all-inboxes"_L1 || key == "favorites:unread"_L1) {
        const bool unreadOnly = (key == "favorites:unread"_L1);
        QSqlQuery q(database);
        q.prepare(R"(
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
        )"_L1);
        q.bindValue(":unread_only"_L1, unreadOnly ? 1 : 0);
        readCountPair(q);

        QSqlQuery qUnread(database);
        qUnread.prepare(R"(
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
        )"_L1);
        qUnread.bindValue(":unread_only"_L1, unreadOnly ? 1 : 0);
        if (qUnread.exec() && qUnread.next())
            unread = qUnread.value(0).toInt();

        QSqlQuery qTotal(database);
        qTotal.prepare(R"(
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
        )"_L1);
        qTotal.bindValue(":unread_only"_L1, unreadOnly ? 1 : 0);
        if (qTotal.exec() && qTotal.next())
            total = qTotal.value(0).toInt();

        if (unreadOnly)
            total = unread;
    } else if (key == "favorites:flagged"_L1 || key.startsWith("local:"_L1)) {
        total = 0;
        unread = 0;
    } else if (key.startsWith("tag:"_L1)) {
        const QString tag = key.mid("tag:"_L1.size()).trimmed().toLower();
        if (!tag.isEmpty()) {
            QSqlQuery q(database);
            if (tag == "important"_L1) {
                q.prepare(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN EXISTS (
                                SELECT 1 FROM message_folder_map x
                                WHERE x.account_email=mfm.account_email
                                  AND x.message_id=mfm.message_id
                                  AND x.unread=1
                           ) THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder) LIKE '%/important'
                )"_L1);
            } else {
                q.prepare(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN EXISTS (
                                SELECT 1 FROM message_folder_map x
                                WHERE x.account_email=mfm.account_email
                                  AND x.message_id=mfm.message_id
                                  AND x.unread=1
                           ) THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder)=:folder
                )"_L1);
                q.bindValue(":folder"_L1, tag);
            }
            readCountPair(q);

            QSqlQuery qUnread(database);
            if (tag == "important"_L1) {
                qUnread.prepare(R"(
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
                )"_L1);
            } else {
                qUnread.prepare(R"(
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
                )"_L1);
                qUnread.bindValue(":folder"_L1, tag);
            }
            if (qUnread.exec() && qUnread.next())
                unread = qUnread.value(0).toInt();

            QSqlQuery qTotal(database);
            if (tag == "important"_L1) {
                qTotal.prepare(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder) LIKE '%/important'
                        GROUP BY mfm.account_email, thread_key
                    )
                )"_L1);
            } else {
                qTotal.prepare(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                        GROUP BY mfm.account_email, thread_key
                    )
                )"_L1);
                qTotal.bindValue(":folder"_L1, tag);
            }
            if (qTotal.exec() && qTotal.next())
                total = qTotal.value(0).toInt();
        }
    } else {
        QString folder = rawFolderName.trimmed().toLower();
        if (folder.isEmpty() && key.startsWith("account:"_L1))
            folder = key.mid("account:"_L1.size());

        if (!folder.isEmpty()) {
            if (folder == "inbox"_L1) {
                QSqlQuery q(database);
                q.prepare(R"(
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
                )"_L1);
                readCountPair(q);

                QSqlQuery qUnread(database);
                qUnread.prepare(R"(
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
                )"_L1);
                if (qUnread.exec() && qUnread.next())
                    unread = qUnread.value(0).toInt();

                QSqlQuery qTotal(database);
                qTotal.prepare(R"(
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
                )"_L1);
                if (qTotal.exec() && qTotal.next())
                    total = qTotal.value(0).toInt();
            } else {
                QSqlQuery q(database);
                q.prepare(R"(
                    SELECT COUNT(DISTINCT mfm.message_id),
                           SUM(CASE WHEN mfm.unread=1 THEN 1 ELSE 0 END)
                    FROM message_folder_map mfm
                    WHERE lower(mfm.folder)=:folder
                )"_L1);
                q.bindValue(":folder"_L1, folder);
                readCountPair(q);

                QSqlQuery qUnread(database);
                qUnread.prepare(R"(
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
                )"_L1);
                qUnread.bindValue(":folder"_L1, folder);
                if (qUnread.exec() && qUnread.next())
                    unread = qUnread.value(0).toInt();

                QSqlQuery qTotal(database);
                qTotal.prepare(R"(
                    SELECT COUNT(*)
                    FROM (
                        SELECT mfm.account_email,
                               COALESCE(NULLIF(trim(cm.gm_thr_id),''), NULLIF(trim(cm.thread_id),''), CAST(cm.id AS TEXT)) AS thread_key
                        FROM message_folder_map mfm
                        JOIN messages cm ON cm.id = mfm.message_id AND cm.account_email = mfm.account_email
                        WHERE lower(mfm.folder)=:folder
                        GROUP BY mfm.account_email, thread_key
                    )
                )"_L1);
                qTotal.bindValue(":folder"_L1, folder);
                if (qTotal.exec() && qTotal.next())
                    total = qTotal.value(0).toInt();
            }
        }
    }

    out.insert("total"_L1, total);
    out.insert("unread"_L1, unread);
    out.insert("newMessages"_L1, newMessageCount(folderKey));

    // Cache for future calls (e.g. after pre-warm, subsequent delegate creations are instant).
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        m_folderStatsCache.insert(key, out);
    }
    return out;
}

// ── Shared avatar initials / color ────────────────────────────────────────────

QString DataStore::avatarInitials(const QString &displayName, const QString &fallback)
{
    const QString raw = displayName.trimmed().isEmpty() ? fallback.trimmed() : displayName.trimmed();
    if (raw.isEmpty())
        return "?"_L1;
    const auto parts = raw.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    QString initials;
    for (const auto &p : parts) {
        if (initials.size() >= 2) break;
        if (!p.isEmpty()) initials += p.at(0).toUpper();
    }
    return initials.isEmpty() ? raw.left(1).toUpper() : initials;
}

QColor DataStore::avatarColor(const QString &displayName, const QString &fallback)
{
    // FNV-1a hash — must match AvatarBadge.qml stableHash().
    const QString key = (displayName + "|"_L1 + fallback).trimmed().toLower();
    const QByteArray input = (key.isEmpty() ? "unknown"_L1 : key).toUtf8();
    quint32 h = 2166136261u;
    for (const char c : input) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    const float hue = static_cast<float>(h % 360) / 360.0f;
    return QColor::fromHslF(hue, 0.50f, 0.45f, 1.0f);
}

QPixmap DataStore::avatarPixmap(const QString &displayName, const QString &email, int size)
{
    const QString initials = avatarInitials(displayName, email);
    const QColor bg = avatarColor(displayName, email);

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

int DataStore::inboxCount() const
{
    auto database = db();
    if (!database.isValid() || !database.isOpen())
        return 0;
    QSqlQuery q(database);
    q.exec("SELECT COUNT(*) FROM message_folder_map WHERE lower(folder)='inbox'"_L1);
    return (q.next()) ? q.value(0).toInt() : 0;
}

void DataStore::incrementNewMessageCount(const QString &rawFolder)
{
    const QString key = rawFolder.trimmed().toLower();
    QMutexLocker lock(&m_newCountMutex);
    m_newMessageCounts[key] += 1;
}

int DataStore::newMessageCount(const QString &folderKey) const
{
    QMutexLocker lock(&m_newCountMutex);
    const QString key = folderKey.trimmed().toLower();

    if (key == "account:inbox"_L1 || key == "favorites:all-inboxes"_L1)
        return m_newMessageCounts.value("inbox"_L1, 0);

    if (key.startsWith("account:"_L1))
        return m_newMessageCounts.value(key.mid(8), 0);

    if (key.startsWith("tag:"_L1)) {
        const QString tag = key.mid(4);
        // For tag:important, sum all /important folder entries
        if (tag == "important"_L1) {
            int sum = 0;
            for (auto it = m_newMessageCounts.cbegin(); it != m_newMessageCounts.cend(); ++it) {
                if (it.key().endsWith("/important"_L1))
                    sum += it.value();
            }
            return sum;
        }
        return m_newMessageCounts.value(tag, 0);
    }

    // Direct lookup for category keys like "[gmail]/categories/primary"
    return m_newMessageCounts.value(key, 0);
}

void DataStore::clearNewMessageCounts(const QString &folderKey)
{
    {
        QMutexLocker lock(&m_newCountMutex);
        const QString key = folderKey.trimmed().toLower();

        if (key == "account:inbox"_L1 || key == "favorites:all-inboxes"_L1) {
            m_newMessageCounts.remove("inbox"_L1);
            auto it = m_newMessageCounts.begin();
            while (it != m_newMessageCounts.end()) {
                if (it.key().contains("/categories/"_L1))
                    it = m_newMessageCounts.erase(it);
                else
                    ++it;
            }
        } else if (key.startsWith("account:"_L1)) {
            m_newMessageCounts.remove(key.mid(8));
        } else if (key.startsWith("tag:"_L1)) {
            const QString tag = key.mid(4);
            if (tag == "important"_L1) {
                auto it = m_newMessageCounts.begin();
                while (it != m_newMessageCounts.end()) {
                    if (it.key().endsWith("/important"_L1))
                        it = m_newMessageCounts.erase(it);
                    else
                        ++it;
                }
            } else {
                m_newMessageCounts.remove(tag);
            }
        } else {
            m_newMessageCounts.remove(key);
        }
    }

    // Invalidate stats cache so QML picks up the zeroed count.
    {
        QMutexLocker lock(&m_folderStatsCacheMutex);
        m_folderStatsCache.clear();
    }
    emit dataChanged();
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
    q.exec(R"(
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
    )"_L1);

    while (q.next()) {
        QVariantMap row;
        row.insert("accountEmail"_L1, q.value(0));
        row.insert("name"_L1, q.value(1));
        row.insert("flags"_L1, q.value(2));
        row.insert("specialUse"_L1, q.value(3));
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
        const QString partId   = a.value("partId"_L1).toString().trimmed();
        const QString name     = a.value("name"_L1).toString().trimmed();
        const QString mimeType = a.value("mimeType"_L1).toString().trimmed();
        const int encodedBytes = a.value("encodedBytes"_L1).toInt();
        const QString encoding = a.value("encoding"_L1).toString().trimmed();

        if (partId.isEmpty()) continue;

        QSqlQuery q(database);
        q.prepare(R"(
            INSERT INTO message_attachments (message_id, account_email, part_id, name, mime_type, encoded_bytes, encoding)
            VALUES (:message_id, :account_email, :part_id, :name, :mime_type, :encoded_bytes, :encoding)
            ON CONFLICT(account_email, message_id, part_id) DO UPDATE SET
              name=excluded.name,
              mime_type=excluded.mime_type,
              encoded_bytes=excluded.encoded_bytes,
              encoding=excluded.encoding
        )"_L1);
        q.bindValue(":message_id"_L1,    messageId);
        q.bindValue(":account_email"_L1, accountEmail);
        q.bindValue(":part_id"_L1,       partId);
        q.bindValue(":name"_L1,          name.isEmpty() ? "Attachment"_L1 : name);
        q.bindValue(":mime_type"_L1,     mimeType);
        q.bindValue(":encoded_bytes"_L1, encodedBytes);
        q.bindValue(":encoding"_L1,      encoding);
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
    q.prepare(R"(
        SELECT DISTINCT ma.part_id, ma.name, ma.mime_type, ma.encoded_bytes, ma.encoding
        FROM message_attachments ma
        JOIN message_folder_map mfm ON mfm.message_id = ma.message_id
                                   AND mfm.account_email = ma.account_email
        WHERE ma.account_email = :account_email
          AND lower(mfm.folder) = lower(:folder)
          AND mfm.uid = :uid
        ORDER BY ma.part_id
    )"_L1);
    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1,        folder.trimmed());
    q.bindValue(":uid"_L1,           uid.trimmed());

    if (!q.exec()) return out;

    while (q.next()) {
        const QString encoding = q.value(4).toString();
        // Report decoded size: base64 encodes 3 bytes as 4 chars → multiply by 3/4.
        const int encodedBytes = q.value(3).toInt();
        const int displayBytes = (encoding.compare("base64"_L1, Qt::CaseInsensitive) == 0)
                                 ? static_cast<int>(encodedBytes * 3 / 4)
                                 : encodedBytes;

        QVariantMap row;
        row.insert("partId"_L1,   q.value(0).toString());
        row.insert("name"_L1,     q.value(1).toString());
        row.insert("mimeType"_L1, q.value(2).toString());
        row.insert("bytes"_L1,    displayBytes);
        row.insert("encoding"_L1, encoding);
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

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

QVariantList DataStore::searchMessages(const QString &query, int limit, int offset, bool *hasMore) const
{
    if (hasMore) *hasMore = false;
    const QString term = query.trimmed();
    if (term.isEmpty()) return {};

    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    const QString pattern = "%"_L1 + term + "%"_L1;

    auto annotateMessageFlags = [&](QVariantList &list) {
        if (list.isEmpty())
            return;

        QStringList messageIds;
        messageIds.reserve(list.size());
        for (const QVariant &v : list) {
            const QString mid = v.toMap().value("messageId"_L1).toString();
            if (!mid.isEmpty())
                messageIds.push_back(mid);
        }

        QSet<QString> importantMessageIds;
        if (!messageIds.isEmpty()) {
            QStringList ph;
            ph.reserve(messageIds.size());
            for (int i = 0; i < messageIds.size(); ++i)
                ph << QStringLiteral(":m%1").arg(i);
            const QString inClause = ph.join(QLatin1Char(','));

            QSqlQuery qImp(database);
            qImp.prepare("SELECT DISTINCT message_id FROM message_folder_map WHERE lower(folder) LIKE '%/important' AND message_id IN (%1)"_L1.arg(inClause));
            for (int i = 0; i < messageIds.size(); ++i)
                qImp.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qImp.exec()) {
                while (qImp.next()) importantMessageIds.insert(qImp.value(0).toString());
            }

            QSqlQuery qImpL(database);
            qImpL.prepare("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)='important' AND message_id IN (%1)"_L1.arg(inClause));
            for (int i = 0; i < messageIds.size(); ++i)
                qImpL.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qImpL.exec()) {
                while (qImpL.next()) importantMessageIds.insert(qImpL.value(0).toString());
            }
        }

        QSet<QString> withAttachments;
        if (!messageIds.isEmpty()) {
            QStringList ph;
            ph.reserve(messageIds.size());
            for (int i = 0; i < messageIds.size(); ++i)
                ph << QStringLiteral(":m%1").arg(i);

            QSqlQuery qa(database);
            qa.prepare("SELECT DISTINCT message_id FROM message_attachments WHERE message_id IN (%1)"_L1.arg(ph.join(QLatin1Char(','))));
            for (int i = 0; i < messageIds.size(); ++i)
                qa.bindValue(QStringLiteral(":m%1").arg(i), messageIds.at(i));
            if (qa.exec()) {
                while (qa.next()) withAttachments.insert(qa.value(0).toString());
            }
        }

        for (int i = 0; i < list.size(); ++i) {
            QVariantMap row = list.at(i).toMap();
            const QString mid = row.value("messageId"_L1).toString();
            row.insert("hasAttachments"_L1,   withAttachments.contains(mid));
            row.insert("isImportant"_L1,      importantMessageIds.contains(mid));
            row.insert("hasTrackingPixel"_L1, row.value("hasTrackingPixel"_L1).toBool());
            list[i] = row;
        }
    };

    const int safeOffset = qMax(0, offset);
    const int chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
    const int targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

    QSet<QString> seenKeys;
    QVariantList out;
    int rawOffset = 0;
    bool exhausted = false;

    while (!exhausted) {
        QSqlQuery q(database);
        q.prepare(R"(
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
            WHERE cm.account_email = mfm.account_email
              AND (cm.sender LIKE :p1 OR cm.subject LIKE :p2 OR cm.snippet LIKE :p3)
            ORDER BY cm.received_at DESC
            LIMIT :limit OFFSET :offset
        )"_L1);
        q.bindValue(":p1"_L1, pattern);
        q.bindValue(":p2"_L1, pattern);
        q.bindValue(":p3"_L1, pattern);
        q.bindValue(":limit"_L1, chunkSize);
        q.bindValue(":offset"_L1, rawOffset);

        int fetchedRaw = 0;
        if (q.exec()) {
            while (q.next()) {
                ++fetchedRaw;

                const QString tid  = q.value(15).toString().trimmed();
                QString gtid       = q.value(17).toString().trimmed();
                const QString acct = q.value(0).toString();
                const QString mid  = q.value(3).toString();
                if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive))
                    gtid = tid.mid(3).trimmed();
                const QString tkey = !gtid.isEmpty()
                    ? acct + "|gtid:"_L1 + gtid
                    : (tid.isEmpty()
                        ? acct + "|msg:"_L1 + mid
                        : acct + "|tid:"_L1 + tid);
                if (seenKeys.contains(tkey)) continue;
                seenKeys.insert(tkey);

                QVariantMap row;
                row.insert("accountEmail"_L1,     q.value(0));
                row.insert("folder"_L1,           q.value(1));
                row.insert("uid"_L1,              q.value(2));
                row.insert("messageId"_L1,        mid);
                row.insert("sender"_L1,           q.value(4));
                row.insert("subject"_L1,          q.value(5));
                row.insert("recipient"_L1,        q.value(6));
                row.insert("recipientAvatarUrl"_L1, q.value(7));
                row.insert("receivedAt"_L1,       q.value(8));
                row.insert("snippet"_L1,          q.value(9));
                row.insert("hasTrackingPixel"_L1, q.value(10).toInt() == 1);
                row.insert("avatarDomain"_L1,     q.value(11));
                row.insert("avatarUrl"_L1,        q.value(12));
                row.insert("avatarSource"_L1,     q.value(13));
                row.insert("unread"_L1,           q.value(14).toInt() == 1);
                row.insert("threadId"_L1,         tid);
                row.insert("threadCount"_L1,      q.value(16).toInt());
                row.insert("gmThrId"_L1,          gtid);
                row.insert("allSenders"_L1,       q.value(18));
                row.insert("flagged"_L1,          q.value(19).toInt() == 1);
                row.insert("isSearchResult"_L1,   true);
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
        const int end = static_cast<int>(qMin(out.size(), static_cast<qsizetype>(safeOffset + limit)));
        if (hasMore) *hasMore = (out.size() > safeOffset + limit);
        out = out.mid(safeOffset, end - safeOffset);
    } else {
        if (hasMore) *hasMore = false;
    }

    annotateMessageFlags(out);
    return out;
}

QVariantList DataStore::recentSearches(int limit) const
{
    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen()) return {};

    QSqlQuery q(database);
    q.prepare("SELECT query, searched_at FROM search_history ORDER BY searched_at DESC LIMIT :limit"_L1);
    q.bindValue(":limit"_L1, limit);

    QVariantList out;
    if (q.exec()) {
        while (q.next()) {
            QVariantMap row;
            row.insert("query"_L1,      q.value(0).toString());
            row.insert("searchedAt"_L1, q.value(1).toString());
            out.push_back(row);
        }
    }
    return out;
}

void DataStore::addRecentSearch(const QString &query)
{
    const QString term = query.trimmed();
    if (term.isEmpty()) return;

    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen()) return;

    QSqlQuery q(database);
    q.prepare("INSERT INTO search_history (query, searched_at) VALUES (:q, datetime('now')) ON CONFLICT(query) DO UPDATE SET searched_at = datetime('now')"_L1);
    q.bindValue(":q"_L1, term);
    q.exec();
}

void DataStore::removeRecentSearch(const QString &query)
{
    const QSqlDatabase database = db();
    if (!database.isValid() || !database.isOpen()) return;

    QSqlQuery q(database);
    q.prepare("DELETE FROM search_history WHERE query = :q"_L1);
    q.bindValue(":q"_L1, query.trimmed());
    q.exec();
}
