#include "messagestore.h"
#include "contactstore.h"
#include "folderstatsstore.h"

#include "../utils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using namespace Qt::Literals::StringLiterals;

namespace {

// ─── Regex constants ────────────────────────────────────────────────

const QRegularExpression kReWhitespace(R"(\s+)"_L1);
const QRegularExpression kReHasLetters("[A-Za-z]"_L1);
const QRegularExpression kReSnippetUrl(R"(https?://\S+)"_L1, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kReSnippetCharsetBoundary(R"(\b(?:charset|boundary)\s*=\s*"?[^"\s]+"?)"_L1, QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kReSnippetViewEmailInBrowser(R"((?i)view\s+(?:this\s+)?email\s+in\s+(?:a|your)?\s*browser[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetViewInBrowser(R"((?i)view\s+in\s+(?:a|your)?\s*browser[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetViewAsWebPage(R"((?i)view\s+as\s+(?:a\s+)?web\s+page[:!\-\s]*(?:https?://\S+)?)"_L1);
const QRegularExpression kReSnippetTrailingPunct(R"(\s*[(){}\[\]|:;.,-]+\s*$)"_L1);
const auto &kReHtmlish = Kestrel::htmlishRe();
const QRegularExpression kReMarkdownLinks(R"(\[[^\]\n]{1,240}\]\(https?://[^\s)]+\))"_L1, QRegularExpression::CaseInsensitiveOption);

// Strip Markdown link syntax [text](url) -> replacement captures just the link text.
const QRegularExpression kReSnippetMarkdownLink(R"(\[([^\[\]\n]{1,160})\]\([^\)\n]{0,300}\)?)"_L1);

// Runs of 4+ repeated separator characters, or space-separated patterns like - - - - -.
const QRegularExpression kReSnippetSeparatorRun(R"([-=_*|#~<>]{4,}|(?:[-=_*|#~<>] ){3,}[-=_*|#~<>]?)"_L1);

// Leftover empty or whitespace-only parentheses after URL/link stripping.
const QRegularExpression kReSnippetEmptyParens(R"(\(\s*\))"_L1);

// ─── SQL placeholder helpers ────────────────────────────────────────

/// Builds a list of SQL bind placeholders like {":p0", ":p1", ...} for `count` items.
QStringList
buildPlaceholders(const qsizetype count, const char *prefix = "u") {
    QStringList ph;
    ph.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        ph << QStringLiteral(":%1%2").arg(QLatin1StringView(prefix)).arg(i);
    }

    return ph;
}

/// Binds each value from `values` to the corresponding placeholder ":prefix0", ":prefix1", ...
void
bindPlaceholders(QSqlQuery &q, const QStringList &values, const char *prefix = "u") {
    for (qsizetype i = 0; i < values.size(); ++i) {
        q.bindValue(QStringLiteral(":%1%2").arg(QLatin1StringView(prefix)).arg(i), values.at(i));
    }
}

// ─── Helper functions ───────────────────────────────────────────────

// Extract the first RFC 5322 Message-ID from a header value.
QString
extractFirstMessageIdFromHeader(const QString &val) {
    if (val.trimmed().isEmpty()) { return {}; }

    static const QRegularExpression re("<([^>\\s]+)>"_L1);

    if (const auto m = re.match(val); m.hasMatch()) {
        return m.captured(1).trimmed().toLower();
    }

    const auto parts = val.trimmed().split(kReWhitespace, Qt::SkipEmptyParts);

    return parts.isEmpty() ? QString() : parts.first().toLower();
}

} // close anonymous namespace (temporarily) for static method definition

QString
MessageStore::computeThreadId(const QString &refs, const QString &irt, const QString &ownMsgId) {
    if (auto fromRefs = extractFirstMessageIdFromHeader(refs); !fromRefs.isEmpty()) {
        return fromRefs;
    }

    if (auto fromIrt = extractFirstMessageIdFromHeader(irt); !fromIrt.isEmpty()) {
        return fromIrt;
    }

    return extractFirstMessageIdFromHeader(ownMsgId);
}

namespace { // re-open anonymous namespace

bool
computeHasTrackingPixel(const QString &bodyHtml, const QString &senderRaw) {
    if (bodyHtml.isEmpty()) { return false; }

    const auto email = ContactStore::extractFirstEmail(senderRaw);
    QString senderDomain;

    {
        const qint32 at = static_cast<qint32>(email.lastIndexOf('@'));
        if (at >= 0) {
            senderDomain = email.mid(at + 1).toLower().trimmed();
            const qint32 colon = static_cast<qint32>(senderDomain.indexOf(':'));
            if (colon > 0) { senderDomain = senderDomain.left(colon); }
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
        if (!widthRe.match(tag).hasMatch() || !heightRe.match(tag).hasMatch()) {
            continue;
        }
        const auto srcM = srcRe.match(tag);
        if (!srcM.hasMatch()) { continue; }
        const QString src = srcM.captured(1);
        const auto hostM = hostRe.match(src);
        if (!hostM.hasMatch()) { continue; }
        QString pixelHost = hostM.captured(1).toLower().trimmed();
        const qint32 colon = static_cast<qint32>(pixelHost.indexOf(':'));
        if (colon > 0) { pixelHost = pixelHost.left(colon); }
        if (!senderDomain.isEmpty() && !pixelHost.isEmpty()
                && (pixelHost == senderDomain || pixelHost.endsWith("." + senderDomain))) {
            continue;
        }
        return true;
    }
    return false;
}

QString
logicalMessageKey(const QString &accountEmail,
                  const QString &sender,
                  const QString &subject,
                  const QString &receivedAt) {
    const QString normalized = Kestrel::normalizeEmail(accountEmail) + "\x1f"_L1
            + sender.trimmed().toLower() + "\x1f"_L1
            + subject.trimmed().toLower() + "\x1f"_L1
            + receivedAt.trimmed();

    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool
isTrashFolderName(const QString &folder) {
    const QString f = folder.trimmed().toLower();
    if (f.isEmpty()) { return false; }

    return f == "trash"_L1
            || f == "[gmail]/trash"_L1
            || f == "[google mail]/trash"_L1
            || f.endsWith("/trash"_L1);
}

// Map raw X-GM-LABELS text to a synthetic category folder name.
QString
inferCategoryFromLabels(const QString &rawLabels) {
    QString l = rawLabels.toLower();
    l.replace('"', ' ');  l.replace('\\', ' ');  l.replace('^', ' ');
    l.replace(':', ' ');  l.replace('/', ' ');    l.replace('-', ' ');

    auto has = [&](const char *needle) { return l.contains(QString::fromLatin1(needle)); };

    if (has("promotions") || has("promotion") || has("categorypromotions") || has("smartlabel_promo")) {
        return "[Gmail]/Categories/Promotions"_L1;
    }
    if (has("social") || has("categorysocial") || has("smartlabel_social")) {
        return "[Gmail]/Categories/Social"_L1;
    }
    if (has("purchases") || has("purchase") || has("categorypurchases") || has("smartlabel_receipt")) {
        return "[Gmail]/Categories/Purchases"_L1;
    }
    if (has("updates") || has("update") || has("categoryupdates") || has("smartlabel_notification")) {
        return "[Gmail]/Categories/Updates"_L1;
    }
    if (has("forums") || has("forum") || has("categoryforums") || has("smartlabel_group")) {
        return "[Gmail]/Categories/Forums"_L1;
    }
    if (has("primary") || has("categorypersonal") || has("smartlabel_personal")) {
        return "[Gmail]/Categories/Primary"_L1;
    }
    return {};
}

qint32
folderEdgeCount(const QSqlDatabase &database, const QString &accountEmail, const QString &folder) {
    QSqlQuery q(database);
    q.prepare(R"(
        SELECT COUNT(*)
        FROM message_folder_map mfm
        JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email
        WHERE mfm.account_email=:account_email AND lower(mfm.folder)=:folder
    )"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed().toLower());
    if (!q.exec() || !q.next()) { return 0; }
    return q.value(0).toInt();
}

qint32
folderOverlapCount(const QSqlDatabase &database,
                   const QString &accountEmail,
                   const QString &folder,
                   const QStringList &uids) {
    if (uids.isEmpty()) { return 0; }

    const auto placeholders = buildPlaceholders(uids.size());

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
    bindPlaceholders(q, uids);
    if (!q.exec() || !q.next()) { return 0; }
    return q.value(0).toInt();
}

qint32
allMailOverlapCount(const QSqlDatabase &database,
                    const QString &accountEmail,
                    const QStringList &uids) {
    if (uids.isEmpty()) { return 0; }

    const auto placeholders = buildPlaceholders(uids.size());

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
    bindPlaceholders(q, uids);
    if (!q.exec() || !q.next()) { return 0; }
    return q.value(0).toInt();
}

// Returns true if a new row was inserted (not just updated).
bool
upsertFolderEdge(const QSqlDatabase &database,
                 const QString &accountEmail,
                 qint32 messageId,
                 const QString &folder,
                 const QString &uid,
                 const QVariant &unread) {
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

qint32
removeOrphanMessages(const QSqlDatabase &database) {
    QSqlQuery q(database);
    q.exec("DELETE FROM messages WHERE id NOT IN (SELECT DISTINCT message_id FROM message_folder_map)"_L1);

    return q.numRowsAffected();
}

// ─── Common 20-column SELECT fragment ──────────────────────────────

/// The standard SELECT ... FROM ... JOIN used by every message-list query.
/// Callers append a WHERE clause (and ORDER BY / LIMIT) after this fragment.
const QString kListSelectColumns = R"(
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
)"_L1;

/// Build a QVariantMap for a list-view message row from the standard 20-column query result.
/// Columns: accountEmail, folder, uid, messageId, sender, subject, recipient,
/// recipientAvatarUrl, receivedAt, snippet, hasTrackingPixel, avatarDomain,
/// avatarUrl, avatarSource, unread, threadId, threadCount, gmThrId, allSenders, flagged.
QVariantMap
buildListRow(const QSqlQuery &q, const QString &messageId, const QString &threadId,
             const QString &gmThrId, const bool forceFlagged = false) {
    return {
        {"accountEmail"_L1,     q.value(0)},
        {"folder"_L1,           q.value(1)},
        {"uid"_L1,              q.value(2)},
        {"messageId"_L1,        messageId},
        {"sender"_L1,           q.value(4)},
        {"subject"_L1,          q.value(5)},
        {"recipient"_L1,        q.value(6)},
        {"recipientAvatarUrl"_L1, q.value(7)},
        {"receivedAt"_L1,       q.value(8)},
        {"snippet"_L1,          q.value(9)},
        {"hasTrackingPixel"_L1, q.value(10).toInt() == 1},
        {"avatarDomain"_L1,     q.value(11)},
        {"avatarUrl"_L1,        q.value(12)},
        {"avatarSource"_L1,     q.value(13)},
        {"unread"_L1,           q.value(14).toInt() == 1},
        {"threadId"_L1,         threadId},
        {"threadCount"_L1,      q.value(16).toInt()},
        {"gmThrId"_L1,          gmThrId},
        {"allSenders"_L1,       q.value(18)},
        {"flagged"_L1,          forceFlagged || q.value(19).toInt() == 1},
    };
}

/// Batch-annotate a message list with hasAttachments, isImportant, and hasTrackingPixel flags.
void
annotateMessageFlags(const QSqlDatabase &database, QVariantList &list) {
    if (list.isEmpty()) { return; }

    QStringList messageIds;
    messageIds.reserve(list.size());
    for (const auto &v : list) {
        if (const auto mid = v.toMap().value("messageId"_L1).toString(); !mid.isEmpty()) {
            messageIds.push_back(mid);
        }
    }

    QSet<QString> importantMessageIds;
    if (!messageIds.isEmpty()) {
        const auto ph = buildPlaceholders(messageIds.size(), "m");
        const auto inClause = ph.join(QLatin1Char(','));

        QSqlQuery qImp(database);
        qImp.prepare("SELECT DISTINCT message_id FROM message_folder_map WHERE lower(folder) LIKE '%/important' AND message_id IN (%1)"_L1.arg(inClause));
        bindPlaceholders(qImp, messageIds, "m");
        if (qImp.exec()) {
            while (qImp.next()) { importantMessageIds.insert(qImp.value(0).toString()); }
        }

        QSqlQuery qImpL(database);
        qImpL.prepare("SELECT DISTINCT message_id FROM message_labels WHERE lower(label)='important' AND message_id IN (%1)"_L1.arg(inClause));
        bindPlaceholders(qImpL, messageIds, "m");
        if (qImpL.exec()) {
            while (qImpL.next()) { importantMessageIds.insert(qImpL.value(0).toString()); }
        }
    }

    QSet<QString> withAttachments;
    if (!messageIds.isEmpty()) {
        const auto ph = buildPlaceholders(messageIds.size(), "m");

        QSqlQuery qa(database);
        qa.prepare("SELECT DISTINCT message_id FROM message_attachments WHERE message_id IN (%1)"_L1.arg(ph.join(QLatin1Char(','))));
        bindPlaceholders(qa, messageIds, "m");
        if (qa.exec()) {
            while (qa.next()) { withAttachments.insert(qa.value(0).toString()); }
        }
    }

    for (auto &entry : list) {
        auto row = entry.toMap();
        const auto mid = row.value("messageId"_L1).toString();
        row.insert("hasAttachments"_L1,   withAttachments.contains(mid));
        row.insert("isImportant"_L1,      importantMessageIds.contains(mid));
        row.insert("hasTrackingPixel"_L1, row.value("hasTrackingPixel"_L1).toBool());
        entry = row;
    }
}

/// Read thread-id / gm_thr_id / account / message_id from a positioned QSqlQuery,
/// build a dedup key, and append via buildListRow if not already seen.
/// Returns true if a row was appended.
bool
appendDedupedRow(QSqlQuery &q, QSet<QString> &seenKeys, QVariantList &out,
                 const bool forceFlagged = false) {
    const auto tid  = q.value(15).toString().trimmed();
    auto gtid       = q.value(17).toString().trimmed();
    const auto acct = q.value(0).toString();
    const auto mid  = q.value(3).toString();

    if (gtid.isEmpty() && tid.startsWith("gm:"_L1, Qt::CaseInsensitive)) {
        gtid = tid.mid(3).trimmed();
    }

    const auto tkey = !gtid.isEmpty()
        ? acct + "|gtid:"_L1 + gtid
        : (tid.isEmpty()
            ? acct + "|msg:"_L1 + mid
            : acct + "|tid:"_L1 + tid);

    if (seenKeys.contains(tkey)) {
        return false;
    }

    seenKeys.insert(tkey);
    out.push_back(buildListRow(q, mid, tid, gtid, forceFlagged));
    return true;
}

// ─── View-mode helpers for messagesForSelection ────────────────────

/// Trash-exclusion NOT EXISTS sub-expression used in several view-mode queries.
const QString kNotInTrash = R"(
    NOT EXISTS (
        SELECT 1 FROM message_folder_map t
        WHERE t.account_email=mfm.account_email
          AND t.message_id=mfm.message_id
          AND (lower(t.folder)='trash' OR lower(t.folder)='[gmail]/trash'
               OR lower(t.folder)='[google mail]/trash' OR lower(t.folder) LIKE '%/trash')
    )
)"_L1;

QVariantList
messagesForFlaggedView(const QSqlDatabase &database, const qint32 limit,
                       const qint32 offset, bool *hasMore) {
    if (!database.isValid() || !database.isOpen()) { return {}; }

    const auto safeOffset = qMax(0, offset);
    QSqlQuery q(database);
    q.prepare(kListSelectColumns + R"(
        WHERE cm.account_email=mfm.account_email
          AND cm.flagged=1
        GROUP BY cm.id
        ORDER BY cm.received_at DESC
        LIMIT :limit OFFSET :offset
    )"_L1);
    q.bindValue(":limit"_L1, limit > 0 ? limit + 1 : 5000);
    q.bindValue(":offset"_L1, safeOffset);

    QVariantList out;
    if (q.exec()) {
        QSet<QString> seenKeys;
        while (q.next()) {
            appendDedupedRow(q, seenKeys, out, /*forceFlagged=*/true);
        }
    }

    if (limit > 0 && out.size() > limit) {
        if (hasMore) { *hasMore = true; }
        out.resize(limit);
    } else {
        if (hasMore) { *hasMore = false; }
    }

    annotateMessageFlags(database, out);
    return out;
}

QVariantList
messagesForInboxesView(const QSqlDatabase &database, const bool unreadOnly,
                       const qint32 limit, const qint32 offset, bool *hasMore) {
    if (!database.isValid() || !database.isOpen()) { return {}; }

    const auto safeOffset = qMax(0, offset);
    const auto chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
    const auto targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

    QSet<QString> seenKeys;
    QVariantList out;
    qint32 rawOffset = 0;
    bool exhausted = false;

    while (!exhausted) {
        QSqlQuery q(database);
        q.prepare(kListSelectColumns + R"(
            WHERE (
                lower(mfm.folder)='inbox'
                OR lower(mfm.folder)='[gmail]/inbox'
                OR lower(mfm.folder)='[google mail]/inbox'
                OR lower(mfm.folder) LIKE '%/inbox'
            )
              AND (:unread_only=0 OR mfm.unread=1)
              AND )" + kNotInTrash + R"(
            ORDER BY cm.received_at DESC
            LIMIT :limit OFFSET :offset
        )"_L1);
        q.bindValue(":unread_only"_L1, unreadOnly ? 1 : 0);
        q.bindValue(":limit"_L1, chunkSize);
        q.bindValue(":offset"_L1, rawOffset);

        qint32 fetchedRaw = 0;
        if (q.exec()) {
            while (q.next()) {
                ++fetchedRaw;
                appendDedupedRow(q, seenKeys, out);
                if (targetCount > 0 && out.size() >= targetCount) {
                    break;
                }
            }
        }

        if (targetCount > 0 && out.size() >= targetCount) {
            break;
        }
        if (fetchedRaw < chunkSize) {
            exhausted = true;
        }
        rawOffset += fetchedRaw;
        if (fetchedRaw == 0) {
            exhausted = true;
        }
    }

    if (limit > 0) {
        if (safeOffset >= out.size()) {
            if (hasMore) { *hasMore = false; }
            return {};
        }
        const auto end = static_cast<qint32>(qMin(out.size(), static_cast<qsizetype>(safeOffset + limit)));
        if (hasMore) { *hasMore = (out.size() > safeOffset + limit); }
        out = out.mid(safeOffset, end - safeOffset);
    } else {
        if (hasMore) { *hasMore = false; }
    }

    annotateMessageFlags(database, out);
    return out;
}

QVariantList
messagesForFolderView(const QSqlDatabase &database, const QString &selectedFolder,
                      const qint32 limit, const qint32 offset, bool *hasMore) {
    if (!database.isValid() || !database.isOpen()) { return {}; }

    const bool selectedIsTrash = isTrashFolderName(selectedFolder);
    const bool selectedIsImportantPseudo = (selectedFolder == "important"_L1);

    const auto safeOffset = qMax(0, offset);
    const auto chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
    const auto targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

    QSet<QString> seenKeys;
    QVariantList out;
    qint32 rawOffset = 0;
    bool exhausted = false;

    while (!exhausted) {
        QSqlQuery q(database);
        if (selectedIsTrash) {
            q.prepare(kListSelectColumns + R"(
                WHERE ((:is_important=1 AND lower(mfm.folder) LIKE '%/important')
                       OR (:is_important=0 AND lower(mfm.folder)=:folder))
                ORDER BY cm.received_at DESC
                LIMIT :limit OFFSET :offset
            )"_L1);
        } else {
            q.prepare(kListSelectColumns + R"(
                WHERE ((:is_important=1 AND lower(mfm.folder) LIKE '%/important')
                       OR (:is_important=0 AND lower(mfm.folder)=:folder))
                  AND )" + kNotInTrash + R"(
                ORDER BY cm.received_at DESC
                LIMIT :limit OFFSET :offset
            )"_L1);
        }
        q.bindValue(":folder"_L1, selectedFolder);
        q.bindValue(":is_important"_L1, selectedIsImportantPseudo ? 1 : 0);
        q.bindValue(":limit"_L1, chunkSize);
        q.bindValue(":offset"_L1, rawOffset);

        qint32 fetchedRaw = 0;
        if (q.exec()) {
            while (q.next()) {
                ++fetchedRaw;
                appendDedupedRow(q, seenKeys, out);
                if (targetCount > 0 && out.size() >= targetCount) {
                    break;
                }
            }
        }

        if (targetCount > 0 && out.size() >= targetCount) {
            break;
        }
        if (fetchedRaw < chunkSize) {
            exhausted = true;
        }
        rawOffset += fetchedRaw;
        if (fetchedRaw == 0) {
            exhausted = true;
        }
    }

    if (limit > 0) {
        if (safeOffset >= out.size()) {
            if (hasMore) { *hasMore = false; }
            return {};
        }
        const auto end = static_cast<qint32>(qMin(out.size(), static_cast<qsizetype>(safeOffset + limit)));
        if (hasMore) { *hasMore = (out.size() > safeOffset + limit); }
        out = out.mid(safeOffset, end - safeOffset);
    } else {
        if (hasMore) { *hasMore = false; }
    }

    annotateMessageFlags(database, out);
    return out;
}

QVariantList
messagesForCategoryView(const QSqlDatabase &database,
                        const QStringList &selectedCategories,
                        const qint32 selectedCategoryIndex,
                        const qint32 limit, const qint32 offset,
                        bool *hasMore) {
    if (!database.isValid() || !database.isOpen()) { return {}; }

    const auto cat = selectedCategories.at(selectedCategoryIndex).toLower();

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

    const auto anySmartLabel =
        "EXISTS (SELECT 1 FROM message_labels ml "
        "WHERE ml.account_email=m.account_email AND ml.message_id=m.message_id "
        "AND (lower(ml.label) LIKE '%/categories/primary%' OR lower(ml.label) LIKE '%/categories/promotion%' "
        "OR lower(ml.label) LIKE '%/categories/social%' OR lower(ml.label) LIKE '%/categories/update%' "
        "OR lower(ml.label) LIKE '%/categories/forum%' OR lower(ml.label) LIKE '%/categories/purchase%'))"_L1;
    const auto inboxMap =
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

    const auto idsSql = R"(
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
        struct Key { QString account; qint64 messageId{}; };
        QVector<Key> keys;
        while (qIds.next()) {
            keys.push_back({qIds.value(0).toString(), qIds.value(1).toLongLong()});
        }
        if (limit > 0 && keys.size() > limit) {
            if (hasMore) { *hasMore = true; }
            keys.resize(limit);
        }

        QSqlQuery qRow(database);
        qRow.prepare(kListSelectColumns + R"(
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

        QSet<QString> seenKeys;
        for (const auto &k : keys) {
            qRow.bindValue(":account_email"_L1, k.account);
            qRow.bindValue(":message_id"_L1, k.messageId);
            qRow.bindValue(":preferred"_L1, preferredFolderLike);
            if (!qRow.exec() || !qRow.next()) { continue; }

            appendDedupedRow(qRow, seenKeys, out);
        }
    }

    annotateMessageFlags(database, out);
    return out;
}

qint32
deleteFolderEdge(const QSqlDatabase &database,
                 const QString &accountEmail,
                 const QString &folder,
                 const QString &uid) {
    QSqlQuery qMap(database);
    qMap.prepare(R"(
        DELETE FROM message_folder_map
        WHERE account_email=:account_email AND folder=:folder AND uid=:uid
    )"_L1);
    qMap.bindValue(":account_email"_L1, accountEmail);
    qMap.bindValue(":folder"_L1, folder);
    qMap.bindValue(":uid"_L1, uid);
    qMap.exec();

    return qMap.numRowsAffected();
}

qint32
pruneFolderEdgesToUids(const QSqlDatabase &database,
                       const QString &accountEmail,
                       const QString &folder,
                       const QStringList &uids) {
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
    QSqlQuery qLocal(database);
    qLocal.prepare("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"_L1);
    qLocal.bindValue(":account_email"_L1, accountEmail);
    qLocal.bindValue(":folder"_L1, folder);
    if (!qLocal.exec()) { return 0; }

    const QSet<QString> remoteSet(uids.begin(), uids.end());
    QStringList toDelete;
    while (qLocal.next()) {
        const QString uid = qLocal.value(0).toString().trimmed();
        if (!uid.isEmpty() && !remoteSet.contains(uid)) {
            toDelete.push_back(uid);
        }
    }

    if (toDelete.isEmpty()) { return 0; }

    const auto placeholders = buildPlaceholders(toDelete.size(), "d");

    QSqlQuery qDel(database);
    qDel.prepare(
        "DELETE FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder) AND uid IN (%1)"_L1
        .arg(placeholders.join(","_L1)));

    qDel.bindValue(":account_email"_L1, accountEmail);
    qDel.bindValue(":folder"_L1, folder);
    bindPlaceholders(qDel, toDelete, "d");
    qDel.exec();

    return qDel.numRowsAffected();
}

// ─── Snippet sanitizer (extracted from inline lambda in upsertHeader) ───

QString
sanitizeSnippet(const QString &snippetRaw, const QString &subjectRaw) {
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
            filtered.append(ch);
        }
        s = filtered.trimmed();

        QString out;
        out.reserve(s.size());
        qint32 spaceRun = 0;
        for (const QChar ch : s) {
            if (ch.isSpace()) {
                ++spaceRun;
                if (spaceRun <= 1) { out.append(' '); }
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

    // Strip Markdown link syntax [text](url) -> keep just the link text.
    s.replace(kReSnippetMarkdownLink, "\\1"_L1);

    const bool hadUrl = kReSnippetUrl.match(s).hasMatch();
    s.replace(kReSnippetUrl, QString());
    s.replace(kReSnippetCharsetBoundary, QString());
    s.replace(kReSnippetMarkdownLink, "\\1"_L1);
    s.replace(kReSnippetEmptyParens, " "_L1);
    s = normalizeSnippetWhitespace(s);

    // Strip runs of repeated separator characters.
    s.replace(kReSnippetSeparatorRun, " "_L1);
    s = normalizeSnippetWhitespace(s);

    // Strip common web-view boilerplate.
    s.replace(kReSnippetViewEmailInBrowser, QString());
    s.replace(kReSnippetViewInBrowser, QString());
    s.replace(kReSnippetViewAsWebPage, QString());
    s = normalizeSnippetWhitespace(s);

    s.replace(kReSnippetTrailingPunct, QString());
    s = normalizeSnippetWhitespace(s);

    const QString t = s.toLower();
    const qint32 alphaCount = static_cast<qint32>(s.count(kReHasLetters));
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
        if (!subject.isEmpty()) { return subject.left(140); }
        if (!originalNormalized.isEmpty()) { return originalNormalized.left(140); }
        return {};
    }

    return s.left(140);
}

/// Inserts a label + tag + tag_map row for a message.
/// Returns true if the label was newly inserted (not already present).
bool
persistLabelAndTag(const QSqlDatabase &database, const QString &accountEmail,
                   const qint32 messageId, const QString &label, const QString &source) {
    QSqlQuery qCheck(database);
    qCheck.prepare("SELECT 1 FROM message_labels WHERE account_email=:a AND message_id=:m AND label=:l"_L1);

    qCheck.bindValue(":a"_L1, accountEmail);
    qCheck.bindValue(":m"_L1, messageId);
    qCheck.bindValue(":l"_L1, label);
    qCheck.exec();
    const bool isNew = !qCheck.next();

    QSqlQuery qLabel(database);
    qLabel.prepare(R"(
        INSERT INTO message_labels (account_email, message_id, label, source, confidence, observed_at)
        VALUES (:account_email, :message_id, :label, :source, 100, datetime('now'))
        ON CONFLICT(account_email, message_id, label) DO UPDATE SET
          source=:source,
          confidence=MAX(message_labels.confidence, 100),
          observed_at=datetime('now')
    )"_L1);

    qLabel.bindValue(":account_email"_L1, accountEmail);
    qLabel.bindValue(":message_id"_L1, messageId);
    qLabel.bindValue(":label"_L1, label);
    qLabel.bindValue(":source"_L1, source);
    qLabel.exec();

    QSqlQuery qTag(database);
    qTag.prepare(R"(
        INSERT INTO tags (name, normalized_name, origin, updated_at)
        VALUES (:name, lower(:name), 'server', datetime('now'))
        ON CONFLICT(normalized_name) DO UPDATE SET updated_at=datetime('now')
    )"_L1);

    qTag.bindValue(":name"_L1, label);
    qTag.exec();

    QSqlQuery qTagMap(database);
    qTagMap.prepare(R"(
        INSERT INTO message_tag_map (account_email, message_id, tag_id, source, observed_at)
        SELECT :account_email, :message_id, id, 'server', datetime('now')
        FROM tags WHERE normalized_name=lower(:label)
        ON CONFLICT(account_email, message_id, tag_id) DO UPDATE SET observed_at=datetime('now')
    )"_L1);

    qTagMap.bindValue(":account_email"_L1, accountEmail);
    qTagMap.bindValue(":message_id"_L1, messageId);
    qTagMap.bindValue(":label"_L1, label);
    qTagMap.exec();

    return isNew;
}

/// Inserts sender/recipient participant rows for a message.
void
persistParticipants(const QSqlDatabase &database, const QString &accountEmail,
                    const qint32 messageId,
                    const QString &senderDisplayName, const QString &senderEmail,
                    const QString &recipientDisplayName, const QString &recipientEmail) {
    QSqlQuery qDel(database);
    qDel.prepare("DELETE FROM message_participants WHERE account_email=:account_email AND message_id=:message_id"_L1);

    qDel.bindValue(":account_email"_L1, accountEmail);
    qDel.bindValue(":message_id"_L1, messageId);
    qDel.exec();

    auto insert = [&](const QString &role, const qint32 position,
                      const QString &displayName, const QString &address, const QString &source) {
        if (address.trimmed().isEmpty() && displayName.trimmed().isEmpty()) { return; }

        QSqlQuery qP(database);
        qP.prepare(R"(
            INSERT INTO message_participants (account_email, message_id, role, position, display_name, address, source)
            VALUES (:account_email, :message_id, :role, :position, :display_name, :address, :source)
            ON CONFLICT(account_email, message_id, role, position) DO UPDATE SET
              display_name=excluded.display_name, address=excluded.address, source=excluded.source
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

    insert("sender"_L1, 0, senderDisplayName, senderEmail, "header"_L1);
    insert("recipient"_L1, 0, recipientDisplayName, recipientEmail, "header"_L1);
}

/// Removes stale non-trash folder edges when a message is observed in Trash.
void
cleanupTrashEdges(const QSqlDatabase &database, const QString &accountEmail, const qint32 messageId) {
    QList<std::pair<QString, QString>> edgesToDelete;
    QSqlQuery qFind(database);
    qFind.prepare(R"(
        SELECT folder, uid
        FROM message_folder_map
        WHERE account_email=:account_email
          AND message_id=:message_id
          AND lower(folder) NOT IN ('trash','[gmail]/trash','[google mail]/trash')
          AND lower(folder) NOT LIKE '%/trash'
    )"_L1);

    qFind.bindValue(":account_email"_L1, accountEmail);
    qFind.bindValue(":message_id"_L1, messageId);
    if (qFind.exec()) {
        while (qFind.next()) {
            edgesToDelete.emplace_back(qFind.value(0).toString(), qFind.value(1).toString());
        }
    }

    for (const auto &[folder, uid] : edgesToDelete) {
        deleteFolderEdge(database, accountEmail, folder, uid);
    }
}

// ─── upsertHeader helpers ──────────────────────────────────────────

/// Result from tryReuseWeakIdentity: whether a strong row was found and reused,
/// and whether the folder edge was newly inserted (not just updated).
struct WeakReuseResult {
    bool reused = false;
    bool newEdge = false;
};

/// When we have no sender/subject/messageId/gmMsgId, try to attach a body
/// update and folder edge to an existing strong row (matched by account+folder+uid).
/// Returns {true} if reuse succeeded and the caller should return early.
WeakReuseResult
tryReuseWeakIdentity(const QSqlDatabase &database,
                     const QString &accountEmail,
                     const QString &folder,
                     const QString &uid,
                     const QString &bodyHtml,
                     const qint32 unread) {
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
    qExisting.bindValue(":folder"_L1, folder);
    qExisting.bindValue(":uid"_L1, uid);
    if (!qExisting.exec() || !qExisting.next()) {
        return {false};
    }

    const auto existingMessageId = qExisting.value(0).toInt();
    const auto existingSender = qExisting.value(1).toString();
    qInfo().noquote() << "[upsert-weak-reuse]"
                      << "account=" << accountEmail
                      << "folder=" << folder
                      << "uid=" << uid
                      << "messageId=" << existingMessageId
                      << "hasBody=" << (!bodyHtml.trimmed().isEmpty());

    if (!bodyHtml.trimmed().isEmpty()) {
        const auto hasTP = computeHasTrackingPixel(bodyHtml, existingSender) ? 1 : 0;
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
        qBody.bindValue(":body_html"_L1, bodyHtml);
        qBody.bindValue(":has_tp"_L1, hasTP);
        qBody.bindValue(":unread"_L1, unread);
        qBody.bindValue(":message_id"_L1, existingMessageId);
        qBody.bindValue(":account_email"_L1, accountEmail);
        qBody.exec();
    }

    const auto newEdge = upsertFolderEdge(database, accountEmail, existingMessageId, folder, uid, unread);
    return {true, newEdge};
}

/// Propagates a Gmail thread_id to all sibling messages that share the same
/// gm_thr_id but have a stale or missing thread_id.
void
backfillSiblingThreadIds(const QSqlDatabase &database,
                         const QString &accountEmail,
                         const QString &gmThrId) {
    if (gmThrId.isEmpty()) { return; }

    const auto newTid = "gm:"_L1 + gmThrId;
    QSqlQuery sibQ(database);
    sibQ.prepare(
        "UPDATE messages SET thread_id=:tid WHERE account_email=:acct "
        "AND gm_thr_id=:gm_thr_id AND (thread_id IS NULL OR thread_id != :tid)"_L1);

    sibQ.bindValue(":tid"_L1,        newTid);
    sibQ.bindValue(":acct"_L1,       accountEmail);
    sibQ.bindValue(":gm_thr_id"_L1,  gmThrId);
    sibQ.exec();
}

/// Persists all observed Gmail labels and the explicit Primary label for a message.
void
persistGmailLabels(const QSqlDatabase &database,
                   const QString &accountEmail,
                   const qint32 messageId,
                   const QString &rawGmailLabels,
                   const bool primaryLabelObserved,
                   FolderStatsStore &folderStats) {
    if (!rawGmailLabels.trimmed().isEmpty()) {
        static const QRegularExpression tokenRe("\"([^\"]+)\"|([^\\s()]+)"_L1);
        auto it = tokenRe.globalMatch(rawGmailLabels);
        while (it.hasNext()) {
            const auto m = it.next();
            auto label = m.captured(1).trimmed();
            if (label.isEmpty()) { label = m.captured(2).trimmed(); }
            if (label.isEmpty()) { continue; }

            persistLabelAndTag(database, accountEmail, messageId, label, "imap-label"_L1);
        }
    }

    if (primaryLabelObserved) {
        const auto primaryLabel = "[Gmail]/Categories/Primary"_L1;
        if (persistLabelAndTag(database, accountEmail, messageId, QString(primaryLabel), "x-gm-labels-primary"_L1)) {
            folderStats.incrementNewMessageCount(QString(primaryLabel));
        }
    }
}

/// Persists avatar URLs and display names for sender and recipient via ContactStore.
void
persistContactData(ContactStore &contacts,
                   const QString &accountEmail,
                   const QString &senderEmail,
                   const QString &senderDisplayName,
                   const QString &avatarUrl,
                   const QString &avatarSource,
                   const QString &recipientEmail,
                   const QString &recipientDisplayName,
                   const QString &recipientAvatarUrl,
                   const bool recipientAvatarLookupMiss) {
    contacts.persistSenderAvatar(senderEmail, avatarUrl, avatarSource);
    contacts.persistRecipientAvatar(recipientEmail, recipientAvatarUrl, recipientAvatarLookupMiss);

    contacts.persistDisplayName(senderEmail, senderDisplayName, "sender-header"_L1);
    if (!recipientEmail.isEmpty()
            && recipientEmail.compare(accountEmail.trimmed(), Qt::CaseInsensitive) != 0) {
        contacts.persistDisplayName(recipientEmail, recipientDisplayName, "recipient-header"_L1);
    }
}

/// Dispatches a desktop notification for a newly inserted unread inbox message.
void
dispatchNewMailNotification(const MessageStoreCallbacks &callbacks,
                            const QString &accountEmail,
                            const QString &folder,
                            const QString &uid,
                            const qint32 unread,
                            const QString &senderDisplayName,
                            const QString &senderEmail,
                            const QString &senderRaw,
                            const QString &subject,
                            const QString &snippet) {
    if (!callbacks.desktopNotifyEnabled()) { return; }
    if (!unread) { return; }
    if (folder.compare("INBOX"_L1, Qt::CaseInsensitive) != 0) { return; }

    QVariantMap info;
    info["senderDisplay"_L1] = senderDisplayName.isEmpty() ? senderEmail : senderDisplayName;
    info["senderRaw"_L1]     = senderRaw;
    info["subject"_L1]       = subject;
    info["snippet"_L1]       = snippet;
    info["accountEmail"_L1]  = accountEmail;
    info["folder"_L1]        = folder;
    info["uid"_L1]           = uid;
    callbacks.onNewMail(info);
}

} // anonymous namespace

// ─── Construction ───────────────────────────────────────────────────

MessageStore::MessageStore(DbAccessor dbAccessor,
                           ContactStore &contacts,
                           FolderStatsStore &folderStats,
                           MessageStoreCallbacks callbacks)
    : m_db(std::move(dbAccessor))
    , m_contacts(contacts)
    , m_folderStats(folderStats)
    , m_callbacks(std::move(callbacks))
{
}

// ─── upsertHeaders / upsertHeader ──────────────────────────────────

void
MessageStore::upsertHeaders(const QVariantList &headers) const {
    if (headers.isEmpty()) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    database.transaction();
    for (const QVariant &v : headers) {
        if (const QVariantMap h = v.toMap(); !h.isEmpty()) {
            upsertHeader(h);
        }
    }
    database.commit();
}

void
MessageStore::upsertHeader(const QVariantMap &header) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const auto accountEmail           = header.value("accountEmail"_L1).toString();
    const auto folderValue            = header.value("folder"_L1, "INBOX"_L1).toString();
    const auto uidValue               = header.value("uid"_L1).toString();
    const auto subjectValue           = header.value("subject"_L1).toString();
    const auto rawSnippetValue        = header.value("snippet"_L1).toString();
    const auto senderValue            = header.value("sender"_L1).toString();
    const auto recipientValue         = header.value("recipient"_L1).toString();
    const auto recipientAvatarUrlValue = header.value("recipientAvatarUrl"_L1).toString().trimmed();
    const auto recipientAvatarLookupMiss = header.value("recipientAvatarLookupMiss"_L1, false).toBool();
    const auto senderEmailValue       = ContactStore::extractFirstEmail(senderValue);
    const auto recipientEmailValue    = ContactStore::extractFirstEmail(recipientValue);
    const auto senderDisplayNameValue = ContactStore::extractExplicitDisplayName(senderValue, senderEmailValue);
    const auto recipientDisplayNameValue = ContactStore::extractExplicitDisplayName(recipientValue, recipientEmailValue);
    const auto receivedAtValue        = header.value("receivedAt"_L1, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).toString();
    const auto snippetValue           = sanitizeSnippet(rawSnippetValue, subjectValue);
    const auto bodyHtmlValue          = header.value("bodyHtml"_L1).toString().trimmed();
    const auto avatarUrlValue         = header.value("avatarUrl"_L1).toString().trimmed();
    const auto avatarSourceValue      = header.value("avatarSource"_L1).toString().trimmed().toLower();
    const auto unreadValue            = header.value("unread"_L1, true).toBool() ? 1 : 0;
    const auto messageIdHeaderValue   = header.value("messageIdHeader"_L1).toString().trimmed();
    const auto gmMsgIdValue           = header.value("gmMsgId"_L1).toString().trimmed();
    const auto gmThrIdValue           = header.value("gmThrId"_L1).toString().trimmed();
    const auto listUnsubscribeValue   = header.value("listUnsubscribe"_L1).toString().trimmed();
    const auto replyToValue           = header.value("replyTo"_L1).toString().trimmed();
    const auto returnPathValue        = header.value("returnPath"_L1).toString().trimmed();
    const auto authResultsValue       = header.value("authResults"_L1).toString().trimmed();
    const auto xMailerValue           = header.value("xMailer"_L1).toString().trimmed();
    const auto inReplyToValue         = header.value("inReplyTo"_L1).toString().trimmed();
    const auto referencesValue        = header.value("references"_L1).toString().trimmed();
    const auto espVendorValue         = header.value("espVendor"_L1).toString().trimmed();
    const auto ccValue                = header.value("cc"_L1).toString().trimmed();
    const auto primaryLabelObserved   = header.value("primaryLabelObserved"_L1, false).toBool();
    const auto rawGmailLabels         = header.value("rawGmailLabels"_L1).toString();

    // ── Phase 1: weak identity detection / orphan reuse ────────────
    const bool weakIdentity = senderValue.trimmed().isEmpty()
            && subjectValue.trimmed().isEmpty()
            && messageIdHeaderValue.isEmpty()
            && gmMsgIdValue.isEmpty();
    if (weakIdentity && !uidValue.trimmed().isEmpty()) {
        const auto result = tryReuseWeakIdentity(database, accountEmail, folderValue,
                                                 uidValue, bodyHtmlValue, unreadValue);
        if (result.reused) {
            if (result.newEdge) {
                m_folderStats.incrementNewMessageCount(folderValue);
            }
            m_callbacks.scheduleDataChanged();
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

    // ── Phase 2: main INSERT...ON CONFLICT ─────────────────────────
    const auto lkey = logicalMessageKey(accountEmail, senderValue, subjectValue, receivedAtValue);

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
        QString threadIdValue;
        if (!gmThrIdValue.isEmpty()) {
            threadIdValue = "gm:"_L1 + gmThrIdValue;
        } else {
            threadIdValue = MessageStore::computeThreadId(referencesValue, inReplyToValue, messageIdHeaderValue);
        }
        qCanon.bindValue(":thread_id"_L1, threadIdValue.isEmpty() ? QVariant() : QVariant(threadIdValue));
    }
    qCanon.exec();

    // ── Phase 3: thread ID backfill ────────────────────────────────
    backfillSiblingThreadIds(database, accountEmail, gmThrIdValue);

    // Look up the canonical message ID for subsequent phases.
    QSqlQuery idQ(database);
    idQ.prepare("SELECT id FROM messages WHERE account_email=:account_email AND logical_key=:logical_key LIMIT 1"_L1);

    idQ.bindValue(":account_email"_L1, accountEmail);
    idQ.bindValue(":logical_key"_L1, lkey);
    if (!idQ.exec() || !idQ.next()) {
        return;
    }

    const auto messageId = idQ.value(0).toInt();

    // ── Phase 4: folder edge management ────────────────────────────
    const bool isCategoryFolder = folderValue.contains("/Categories/"_L1, Qt::CaseInsensitive);
    if (!isCategoryFolder) {
        if (upsertFolderEdge(database, accountEmail, messageId, folderValue, uidValue, unreadValue)) {
            m_folderStats.incrementNewMessageCount(folderValue);
            if (!rawGmailLabels.isEmpty()) {
                const auto catFolder = inferCategoryFromLabels(rawGmailLabels);
                if (!catFolder.isEmpty()) {
                    m_folderStats.incrementNewMessageCount(catFolder);
                }
            }
            // Phase 7: notification dispatch.
            dispatchNewMailNotification(m_callbacks, accountEmail, folderValue, uidValue,
                                       unreadValue, senderDisplayNameValue, senderEmailValue,
                                       senderValue, subjectValue, snippetValue);
        }
    } else {
        if (persistLabelAndTag(database, accountEmail, messageId, folderValue, "category-folder-sync"_L1)) {
            m_folderStats.incrementNewMessageCount(folderValue);
        }
    }

    // ── Phase 6: participant + contact persistence ─────────────────
    persistParticipants(database, accountEmail, messageId,
                        senderDisplayNameValue, senderEmailValue,
                        recipientDisplayNameValue, recipientEmailValue);

    persistContactData(m_contacts, accountEmail,
                       senderEmailValue, senderDisplayNameValue,
                       avatarUrlValue, avatarSourceValue,
                       recipientEmailValue, recipientDisplayNameValue,
                       recipientAvatarUrlValue, recipientAvatarLookupMiss);

    // ── Phase 5: label / tag persistence ───────────────────────────
    persistGmailLabels(database, accountEmail, messageId,
                       rawGmailLabels, primaryLabelObserved, m_folderStats);

    // ── Phase 8: attachment upsert ─────────────────────────────────
    {
        const auto attachments = header.value("attachments"_L1).toList();
        if (!attachments.isEmpty()) {
            upsertAttachments(messageId, accountEmail, attachments);
        }
    }

    // Server truth: when a message is in Trash, remove stale non-trash edges.
    if (isTrashFolderName(folderValue)) {
        cleanupTrashEdges(database, accountEmail, messageId);
    }

    m_callbacks.scheduleDataChanged();
}

// ─── Prune / remove ────────────────────────────────────────────────

void
MessageStore::pruneFolderToUids(const QString &accountEmail, const QString &folder, const QStringList &uids) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const QString acc = accountEmail.trimmed();
    const QString fld = folder.trimmed();
    if (acc.isEmpty() || fld.isEmpty()) { return; }

    if (folder.contains("/Categories/"_L1, Qt::CaseInsensitive)) {
        qInfo().noquote() << "[prune-skip]" << "folder=" << folder << "reason=category-folders-are-labels-only";
        return;
    }
    if (FolderStatsStore::isCategoryFolderName(fld) && !uids.isEmpty()) {
        const qint32 existingCount = folderEdgeCount(database, acc, fld);
        const qint32 overlapCount = folderOverlapCount(database, acc, fld, uids);
        const qint32 allMailOverlap = allMailOverlapCount(database, acc, uids);
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

    const qint32 removedFolderRows = pruneFolderEdgesToUids(database, acc, fld, uids);

    const auto removedCanonicalRows = removeOrphanMessages(database);

    if (removedFolderRows > 0 || removedCanonicalRows > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

void
MessageStore::removeAccountUidsEverywhere(const QString &accountEmail, const QStringList &uids,
                                          bool skipOrphanCleanup) const {
    if (uids.isEmpty()) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }


    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) { return; }

    const auto placeholders = buildPlaceholders(uids.size());

    QList<std::pair<QString, QString>> edgesToDelete;
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
    bindPlaceholders(qFind, uids);
    if (qFind.exec()) {
        while (qFind.next()) {
            edgesToDelete.emplace_back(qFind.value(0).toString(), qFind.value(1).toString());
        }
    }
    qint32 removedFolderRows = 0;
    for (const auto &[folder, uid] : edgesToDelete) {
        removedFolderRows += deleteFolderEdge(database, acc, folder, uid);
    }

    qint32 removedCanonicalRows = 0;
    if (!skipOrphanCleanup) {
        removedCanonicalRows = removeOrphanMessages(database);
    }

    qInfo().noquote() << "[prune-delete]"
                      << "account=" << acc
                      << "uidCount=" << uids.size()
                      << "removedFolderRows=" << removedFolderRows
                      << "removedCanonicalRows=" << removedCanonicalRows;

    if (removedFolderRows > 0 || removedCanonicalRows > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

// ─── Flag reconciliation ───────────────────────────────────────────

void
MessageStore::markMessageRead(const QString &accountEmail, const QString &uid) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty() || uid.trimmed().isEmpty()) { return; }

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

    // Signal emission handled by DataStore facade.
}

void
MessageStore::reconcileReadFlags(const QString &accountEmail, const QString &folder,
                                 const QStringList &readUids) const {
    if (readUids.isEmpty()) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) { return; }

    const auto placeholders = buildPlaceholders(readUids.size());

    QSqlQuery qMap(database);
    qMap.prepare(
        "UPDATE message_folder_map SET unread=0 "
        "WHERE account_email=:acc AND unread=1 AND message_id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));

    qMap.bindValue(":acc"_L1,    acc);
    qMap.bindValue(":folder"_L1, folder);
    bindPlaceholders(qMap, readUids);
    qMap.exec();

    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET unread=0 "
        "WHERE unread=1 AND id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));

    qMsg.bindValue(":acc"_L1,    acc);
    qMsg.bindValue(":folder"_L1, folder);
    bindPlaceholders(qMsg, readUids);
    qMsg.exec();

    const qint32 edgesUpdated = qMap.numRowsAffected();
    if (edgesUpdated > 0) {
        qInfo().noquote() << "[reconcile-flags]" << "acc=" << acc
                          << "folder=" << folder
                          << "readUids=" << readUids.size()
                          << "edgesUpdated=" << edgesUpdated;
        m_callbacks.scheduleDataChanged();
    }
}

void
MessageStore::markMessageFlagged(const QString &accountEmail, const QString &uid, const bool flagged) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty() || uid.trimmed().isEmpty()) { return; }

    const qint32 val = flagged ? 1 : 0;
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

    // Signal emission handled by DataStore facade.
}

void
MessageStore::reconcileFlaggedUids(const QString &accountEmail, const QString &folder,
                                   const QStringList &flaggedUids) const {
    if (flaggedUids.isEmpty()) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const QString acc = accountEmail.trimmed();
    if (acc.isEmpty()) { return; }

    const auto placeholders = buildPlaceholders(flaggedUids.size());

    QSqlQuery qMsg(database);
    qMsg.prepare(
        "UPDATE messages SET flagged=1 "
        "WHERE id IN ("
        "  SELECT DISTINCT message_id FROM message_folder_map "
        "  WHERE account_email=:acc AND lower(folder)=lower(:folder) AND uid IN (%1)"
        ")"_L1.arg(placeholders.join(","_L1)));

    qMsg.bindValue(":acc"_L1,    acc);
    qMsg.bindValue(":folder"_L1, folder);
    bindPlaceholders(qMsg, flaggedUids);
    qMsg.exec();

    if (qMsg.numRowsAffected() > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

// ─── Edge CRUD ─────────────────────────────────────────────────────

QVariantMap
MessageStore::folderMapRowForEdge(const QString &accountEmail,
                                  const QString &folder,
                                  const QString &uid) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    QSqlQuery q(database);
    q.prepare("SELECT message_id, unread FROM message_folder_map "
              "WHERE account_email=:acc AND folder=:folder AND uid=:uid LIMIT 1"_L1);

    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":uid"_L1,    uid);
    if (!q.exec() || !q.next()) { return {}; }

    QVariantMap row;
    row.insert("messageId"_L1, q.value(0).toLongLong());
    row.insert("unread"_L1,    q.value(1).toInt());

    return row;
}

void
MessageStore::deleteSingleFolderEdge(const QString &accountEmail,
                                     const QString &folder,
                                     const QString &uid) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    const auto removed = deleteFolderEdge(database, accountEmail, folder, uid);
    const auto removedOrphans = removeOrphanMessages(database);
    if (removed > 0 || removedOrphans > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

QString
MessageStore::folderUidForMessageId(const QString &accountEmail, const QString &folder, const qint64 messageId) const {
    if (messageId <= 0) { return {}; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    QSqlQuery q(database);
    q.prepare("SELECT uid FROM message_folder_map "
              "WHERE account_email=:acc AND lower(folder)=lower(:folder) AND message_id=:mid "
              "LIMIT 1"_L1);

    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":mid"_L1,    messageId);
    if (!q.exec() || !q.next()) { return {}; }

    return q.value(0).toString();
}

void
MessageStore::deleteFolderEdgesForMessage(const QString &accountEmail, const QString &folder, const qint64 messageId) const {
    if (messageId <= 0) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare("DELETE FROM message_folder_map "
              "WHERE account_email=:acc AND lower(folder)=lower(:folder) AND message_id=:mid"_L1);

    q.bindValue(":acc"_L1,    accountEmail);
    q.bindValue(":folder"_L1, folder);
    q.bindValue(":mid"_L1,    messageId);
    q.exec();
    const auto removed = q.numRowsAffected();
    const auto removedOrphans = removeOrphanMessages(database);
    if (removed > 0 || removedOrphans > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

void
MessageStore::insertFolderEdge(const QString &accountEmail, const qint64 messageId,
                               const QString &folder, const QString &uid, const qint32 unread) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

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
        m_folderStats.incrementNewMessageCount(folder);
        if (m_callbacks.desktopNotifyEnabled()
            && unread
            && folder.compare("INBOX"_L1, Qt::CaseInsensitive) == 0) {
            QSqlQuery qMsg(database);
            qMsg.prepare("SELECT sender, subject, snippet FROM messages WHERE id=:id LIMIT 1"_L1);
            qMsg.bindValue(":id"_L1, messageId);
            if (qMsg.exec() && qMsg.next()) {
                const QString rawSender = qMsg.value(0).toString().trimmed();
                const QString se = ContactStore::extractFirstEmail(rawSender);
                const QString sn = ContactStore::extractExplicitDisplayName(rawSender, se);
                QVariantMap info;
                info["senderDisplay"_L1] = sn.isEmpty() ? se : sn;
                info["senderRaw"_L1]     = rawSender;
                info["subject"_L1]       = qMsg.value(1).toString();
                info["snippet"_L1]       = qMsg.value(2).toString();
                info["accountEmail"_L1]  = accountEmail;
                info["folder"_L1]        = folder;
                info["uid"_L1]           = uid;
                m_callbacks.onNewMail(info);
            }
        }
    }
    removeOrphanMessages(database);
    m_callbacks.scheduleDataChanged();
}

QMap<QString, qint64>
MessageStore::lookupByMessageIdHeaders(const QString &accountEmail,
                                       const QStringList &messageIdHeaders) const {
    QMap<QString, qint64> result;
    if (messageIdHeaders.isEmpty()) { return result; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return result; }


    const auto placeholders = buildPlaceholders(messageIdHeaders.size(), "mid");

    QSqlQuery q(database);
    q.prepare(
        "SELECT message_id_header, id FROM messages "
        "WHERE account_email = :account_email "
        "  AND message_id_header IN (%1) "
        "  AND message_id_header IS NOT NULL "
        "  AND length(trim(message_id_header)) > 0"_L1
        .arg(placeholders.join(u',')));
    q.bindValue(u":account_email"_s, accountEmail);
    bindPlaceholders(q, messageIdHeaders, "mid");

    if (!q.exec()) { return result; }
    while (q.next()) {
        result.insert(q.value(0).toString(), q.value(1).toLongLong());
    }

    return result;
}

void
MessageStore::removeAllEdgesForMessageId(const QString &accountEmail, const qint64 messageId) const {
    if (messageId <= 0) { return; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    QSqlQuery q(database);
    q.prepare("DELETE FROM message_folder_map WHERE account_email=:acc AND message_id=:mid"_L1);

    q.bindValue(":acc"_L1, accountEmail);
    q.bindValue(":mid"_L1, messageId);
    q.exec();
    const auto removedEdges = q.numRowsAffected();
    const auto removedOrphans = removeOrphanMessages(database);
    if (removedEdges > 0 || removedOrphans > 0) {
        m_callbacks.scheduleDataChanged();
    }
}

// ─── Folder UID queries ────────────────────────────────────────────

QStringList
MessageStore::folderUids(const QString &accountEmail, const QString &folder) const {
    QStringList out;
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    QSqlQuery q(database);
    q.prepare(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id AND m.account_email = mfm.account_email "
        "WHERE mfm.account_email=:account_email AND lower(mfm.folder)=lower(:folder)"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec()) { return out; }
    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty()) { out.push_back(uid); }
    }

    return out;
}

QStringList
MessageStore::folderUidsWithNullSnippet(const QString &accountEmail, const QString &folder) const {
    QStringList out;
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

    QSqlQuery q(database);
    q.prepare(
        "SELECT mfm.uid FROM message_folder_map mfm "
        "JOIN messages m ON m.id = mfm.message_id "
        "WHERE mfm.account_email = :account_email AND lower(mfm.folder) = lower(:folder) "
        "AND (m.snippet IS NULL OR m.snippet = '')"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec()) { return out; }
    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty()) { out.push_back(uid); }
    }

    return out;
}

qint64
MessageStore::folderMaxUid(const QString &accountEmail, const QString &folder) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return 0;
    }

    QSqlQuery q(database);
    q.prepare("SELECT uid FROM message_folder_map WHERE account_email=:account_email AND lower(folder)=lower(:folder)"_L1);

    q.bindValue(":account_email"_L1, accountEmail.trimmed());
    q.bindValue(":folder"_L1, folder.trimmed());
    if (!q.exec()) { return 0; }

    qint64 maxUid = 0;
    while (q.next()) {
        bool ok = false;
        const qint64 v = q.value(0).toString().toLongLong(&ok);
        if (ok && v > maxUid) { maxUid = v; }
    }

    return maxUid;
}

qint64
MessageStore::folderMessageCount(const QString &accountEmail, const QString &folder) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return 0; }

    return folderEdgeCount(database, accountEmail, folder);
}

// ─── Body fetch candidates ─────────────────────────────────────────

QStringList
MessageStore::bodyFetchCandidates(const QString &accountEmail, const QString &folder,
                                  const qint32 limit) const {
    QStringList out;

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    const qint32 boundedLimit = qBound(1, limit, 100);

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

    if (!q.exec()) {
        return out;
    }

    while (q.next()) {
        const QString uid = q.value(0).toString().trimmed();
        if (!uid.isEmpty()) {
            out.push_back(uid);
        }
    }

    return out;
}

QVariantList
MessageStore::bodyFetchCandidatesByAccount(const QString &accountEmail, const qint32 limit) const {
    QVariantList out;

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

    const qint32 boundedLimit = qBound(1, limit, 100);

    const auto acc = accountEmail.trimmed();
    QSet<qint64> seenMessageIds;

    const auto fetchCandidates = [&](const QString &timeWindow) -> bool {
        QSqlQuery q(database);
        const auto timeClause = timeWindow.isEmpty()
            ? QString()
            : "  AND datetime(m.received_at) >= datetime('now', '%1')"_L1.arg(timeWindow);

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
              ))"_L1 + timeClause + R"(
            ORDER BY CASE WHEN lower(mfm.folder)='inbox' THEN 0 ELSE 1 END,
                     datetime(m.received_at) DESC,
                     m.id DESC
        )"_L1);

        q.bindValue(":account_email"_L1, acc);

        if (!q.exec()) {
            return false;
        }

        while (q.next()) {
            const auto messageId = q.value(0).toLongLong();
            if (seenMessageIds.contains(messageId)) {
                continue;
            }

            seenMessageIds.insert(messageId);

            QVariantMap row;
            row.insert("messageId"_L1, messageId);
            row.insert("folder"_L1, q.value(1).toString());
            row.insert("uid"_L1, q.value(2).toString());
            out.push_back(row);

            if (out.size() >= boundedLimit) {
                break;
            }
        }

        return true;
    };

    // Try recent messages first (3-month window), then fall back to all.
    fetchCandidates("-3 months"_L1);
    if (out.isEmpty()) {
        fetchCandidates(QString());
    }

    return out;
}

QVariantList
MessageStore::fetchCandidatesForMessageKey(const QString &accountEmail,
                                           const QString &folder,
                                           const QString &uid) const {
    QVariantList out;
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return out;
    }

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
    if (!q.exec()) { return out; }
    while (q.next()) {
        const QString f = q.value(0).toString().trimmed();
        const QString u = q.value(1).toString().trimmed();
        if (f.isEmpty() || u.isEmpty()) { continue; }
        QVariantMap row;
        row.insert("folder"_L1, f);
        row.insert("uid"_L1, u);
        out.push_back(row);
    }

    return out;
}

// ─── Message queries ───────────────────────────────────────────────

bool
MessageStore::hasUsableBodyForEdge(const QString &accountEmail, const QString &folder, const QString &uid) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) {
        return false;
    }

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

    if (!q.exec() || !q.next()) {
        return false;
    }

    const QString html = q.value(0).toString();
    if (html.trimmed().isEmpty()) {
        return false;
    }

    const QString lower = html.toLower();
    if (lower.contains("ok success [throttled]"_L1) || lower.contains("authenticationfailed"_L1)) {
        return false;
    }

    if (lower.contains("cid:"_L1)) {
        return false;
    }

    const qint32 tableOpen  = static_cast<qint32>(lower.count("<table"_L1));
    const qint32 tableClose = static_cast<qint32>(lower.count("</table>"_L1));
    if (tableClose > tableOpen) {
        return false;
    }

    return true;
}

QVariantMap
MessageStore::messageByKey(const QString &accountEmail, const QString &folder, const QString &uid) const {
    QVariantMap row;
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return row; }

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
    if (!q.exec() || !q.next()) { return row; }

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

QVariantList
MessageStore::messagesForThread(const QString &accountEmail, const QString &threadId) const {
    const QSqlDatabase database = m_db();
    if (!database.isValid() || !database.isOpen() || threadId.trimmed().isEmpty()) {
        return {};
    }

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
    if (!q.exec()) { return {}; }

    QVariantList result;
    QSet<qint32> seen;
    while (q.next()) {
        const qint32 msgId = q.value(3).toInt();
        if (seen.contains(msgId)) { continue; }
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

bool
MessageStore::updateBodyForKey(const QString &accountEmail,
                               const QString &folder,
                               const QString &uid,
                               const QString &bodyHtml) const {
    const QString html = bodyHtml.trimmed();
    if (html.isEmpty()) { return false; }

    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return false; }

    qint32 prevLen = -1;
    qint32 prevTP = 0;
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

    const qint32 hasTP = computeHasTrackingPixel(html, senderForTP) ? 1 : 0;

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
    if (!q.exec()) { return false; }

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

    // Only reload the list when the tracking-pixel flag changed.
    if (changed && hasTP != prevTP) {
        m_callbacks.scheduleDataChanged();
    }

    // Signal emission (bodyHtmlUpdated) handled by DataStore facade.

    return changed;
}

// ─── Selection / list queries ──────────────────────────────────────

QVariantList
MessageStore::messagesForSelection(const QString &folderKey,
                                   const QStringList &selectedCategories,
                                   const qint32 selectedCategoryIndex,
                                   const qint32 limit,
                                   const qint32 offset,
                                   bool *hasMore) const {
    const QSqlDatabase database = m_db();
    const auto key = folderKey.trimmed();
    if (hasMore) { *hasMore = false; }

    if (key.startsWith("local:"_L1, Qt::CaseInsensitive)) {
        return {};
    }

    if (key.compare("favorites:flagged"_L1, Qt::CaseInsensitive) == 0) {
        return messagesForFlaggedView(database, limit, offset, hasMore);
    }

    if (key.compare("favorites:all-inboxes"_L1, Qt::CaseInsensitive) == 0
        || key.compare("favorites:unread"_L1, Qt::CaseInsensitive) == 0) {
        const bool unreadOnly = key.compare("favorites:unread"_L1, Qt::CaseInsensitive) == 0;
        return messagesForInboxesView(database, unreadOnly, limit, offset, hasMore);
    }

    QString selectedFolder;
    if (key.startsWith("account:"_L1, Qt::CaseInsensitive)) {
        selectedFolder = key.mid("account:"_L1.size()).toLower();
    } else if (key.startsWith("tag:"_L1, Qt::CaseInsensitive)) {
        selectedFolder = key.mid("tag:"_L1.size()).toLower();
    }

    const bool categoryView = (selectedFolder == "inbox"_L1
                               && !selectedCategories.isEmpty()
                               && selectedCategoryIndex >= 0
                               && selectedCategoryIndex < selectedCategories.size());

    if (!selectedFolder.isEmpty() && !categoryView) {
        return messagesForFolderView(database, selectedFolder, limit, offset, hasMore);
    }

    if (categoryView) {
        return messagesForCategoryView(database, selectedCategories,
                                       selectedCategoryIndex, limit, offset, hasMore);
    }

    return {};
}

QVariantList
MessageStore::groupedMessagesForSelection(const QString &folderKey,
                                          const QStringList &selectedCategories,
                                          const qint32 selectedCategoryIndex,
                                          const bool todayExpanded,
                                          const bool yesterdayExpanded,
                                          const bool lastWeekExpanded,
                                          const bool twoWeeksAgoExpanded,
                                          const bool olderExpanded) const {
    const QVariantList rows = messagesForSelection(folderKey, selectedCategories, selectedCategoryIndex);

    auto bucketKeyForDate = [](const QString &dateValue) -> QString {
        const QDateTime dt = QDateTime::fromString(dateValue, Qt::ISODate);
        if (!dt.isValid()) { return "older"_L1; }
        const QDate target = dt.toLocalTime().date();
        const QDate today = QDate::currentDate();
        const qint32 diffDays = static_cast<qint32>(target.daysTo(today));
        if (diffDays <= 0) { return "today"_L1; }
        if (diffDays == 1) { return "yesterday"_L1; }

        const QDate weekStart = today.addDays(-(today.dayOfWeek() % 7));
        if (target >= weekStart && target < today) {
            return QStringLiteral("weekday-%1").arg(target.dayOfWeek());
        }

        if (diffDays <= 14) { return "lastWeek"_L1; }
        if (diffDays <= 21) { return "twoWeeksAgo"_L1; }
        return "older"_L1;
    };

    auto bucketLabel = [](const QString &bucketKey) -> QString {
        if (bucketKey == "today"_L1) { return "Today"_L1; }
        if (bucketKey == "yesterday"_L1) { return "Yesterday"_L1; }
        if (bucketKey.startsWith("weekday-"_L1)) {
            bool ok = false;
            if (const auto dow = QStringView{bucketKey}.mid("weekday-"_L1.size()).toInt(&ok); ok && dow >= 1 && dow <= 7) {
                return QLocale().dayName(dow, QLocale::LongFormat);
            }
        }
        if (bucketKey == "lastWeek"_L1) { return "Last Week"_L1; }
        if (bucketKey == "twoWeeksAgo"_L1) { return "Two Weeks Ago"_L1; }
        return "Older"_L1;
    };

    auto isExpanded = [&](const QString &bucketKey) {
        if (bucketKey == "today"_L1) { return todayExpanded; }
        if (bucketKey == "yesterday"_L1) { return yesterdayExpanded; }
        if (bucketKey.startsWith("weekday-"_L1)) { return true; }
        if (bucketKey == "lastWeek"_L1) { return lastWeekExpanded; }
        if (bucketKey == "twoWeeksAgo"_L1) { return twoWeeksAgoExpanded; }
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
        if (rowsInBucket.isEmpty()) { continue; }

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

// ─── Attachments ───────────────────────────────────────────────────

void
MessageStore::upsertAttachments(const qint64 messageId, const QString &accountEmail, const QVariantList &attachments) const {
    auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return; }

    for (const QVariant &v : attachments) {
        const QVariantMap a = v.toMap();
        const QString partId   = a.value("partId"_L1).toString().trimmed();
        const QString name     = a.value("name"_L1).toString().trimmed();
        const QString mimeType = a.value("mimeType"_L1).toString().trimmed();
        const qint32 encodedBytes = a.value("encodedBytes"_L1).toInt();
        const QString encoding = a.value("encoding"_L1).toString().trimmed();

        if (partId.isEmpty()) { continue; }

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

QVariantList
MessageStore::attachmentsForMessage(const QString &accountEmail, const QString &folder, const QString &uid) const {
    QVariantList out;
    const auto database = m_db();
    if (!database.isValid() || !database.isOpen()) { return out; }

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

    if (!q.exec()) { return out; }

    while (q.next()) {
        const QString encoding = q.value(4).toString();
        const qint32 encodedBytes = q.value(3).toInt();
        const qint32 displayBytes = (encoding.compare("base64"_L1, Qt::CaseInsensitive) == 0)
                                    ? static_cast<qint32>(encodedBytes * 3 / 4)
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

// ─── Search ────────────────────────────────────────────────────────

QVariantList
MessageStore::searchMessages(const QString &query, const qint32 limit, const qint32 offset, bool *hasMore) const {
    if (hasMore) { *hasMore = false; }
    const QString term = query.trimmed();
    if (term.isEmpty()) { return {}; }

    const QSqlDatabase database = m_db();
    if (!database.isValid() || !database.isOpen()) { return {}; }

    const QString pattern = "%"_L1 + term + "%"_L1;

    const qint32 safeOffset = qMax(0, offset);
    const qint32 chunkSize = (limit > 0) ? qMax(200, limit * 4) : 5000;
    const qint32 targetCount = (limit > 0) ? (safeOffset + limit + 1) : -1;

    QSet<QString> seenKeys;
    QVariantList out;
    qint32 rawOffset = 0;
    bool exhausted = false;

    while (!exhausted) {
        QSqlQuery q(database);
        q.prepare(kListSelectColumns + R"(
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

        qint32 fetchedRaw = 0;
        if (q.exec()) {
            while (q.next()) {
                ++fetchedRaw;

                const auto added = appendDedupedRow(q, seenKeys, out);
                if (added) {
                    auto row = out.last().toMap();
                    row.insert("isSearchResult"_L1, true);
                    out.last() = row;
                }

                if (targetCount > 0 && out.size() >= targetCount) {
                    break;
                }
            }
        }

        if (targetCount > 0 && out.size() >= targetCount) {
            break;
        }
        if (fetchedRaw < chunkSize) {
            exhausted = true;
        }
        rawOffset += fetchedRaw;
        if (fetchedRaw == 0) {
            exhausted = true;
        }
    }

    if (limit > 0) {
        if (safeOffset >= out.size()) {
            if (hasMore) { *hasMore = false; }
            return {};
        }
        const qint32 end = static_cast<qint32>(qMin(out.size(), static_cast<qsizetype>(safeOffset + limit)));
        if (hasMore) { *hasMore = (out.size() > safeOffset + limit); }
        out = out.mid(safeOffset, end - safeOffset);
    } else {
        if (hasMore) { *hasMore = false; }
    }

    annotateMessageFlags(database, out);
    return out;
}
