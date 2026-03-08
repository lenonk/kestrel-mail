#include "foldersync.h"
#include "syncutils.h"
#include "kestreltimer.h"
#include "../parser/responseparser.h"
#include "../connection/imapconnection.h"
#include "../message/snippetutils.h"
#include "../message/addressutils.h"
#include "../message/avatarresolver.h"
#include "../message/bodyprocessor.h"

#include <QRegularExpression>
#include <QDateTime>
#include <QThread>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QDebug>
#include <algorithm>
#include <array>
#include <climits>
#include <span>
#include <vector>
#include <tuple>

using namespace Qt::Literals::StringLiterals;

// Parser utilities
using Imap::Parser::extractField;
using Imap::Parser::extractHeaderFieldsLiteral;
using Imap::Parser::extractGmailLabelsRaw;
using Imap::Parser::extractGmailCategoryFolder;
using Imap::Parser::extractInternalDateRaw;
using Imap::Parser::parseBestDateTime;
using Imap::Parser::parseUidSearchAll;
using Imap::Parser::parseSearchIds;

// Snippet utilities
using Imap::SnippetUtils::decodeRfc2047;
using Imap::SnippetUtils::compileDeterministicSnippet;
using Imap::SnippetUtils::cleanSnippet;
using Imap::SnippetUtils::snippetQualityScore;
using Imap::SnippetUtils::snippetLooksLikeProtocolOrJunk;

// Address utilities
using Imap::AddressUtils::extractEmailAddress;
using Imap::AddressUtils::normalizeSenderValue;
using Imap::AddressUtils::sanitizeAddressHeader;
using Imap::AvatarResolver::extractListIdDomain;

// Avatar utilities
using Imap::AvatarResolver::extractBimiLogoUrl;
using Imap::AvatarResolver::senderDomainFromHeader;
using Imap::AvatarResolver::resolveBimiLogoUrlViaDoh;
using Imap::AvatarResolver::resolveFaviconLogoUrl;
using Imap::AvatarResolver::resolveGooglePeopleAvatarUrl;
using Imap::AvatarResolver::resolveGravatarUrl;
using Imap::AvatarResolver::fetchAvatarBlob;

// Body processor
using Imap::BodyProcessor::extractBodySnippetFromFetch;

namespace Imap {

// ─────────────────────────────────────────────────────────────────────────────
// Anonymous namespace — implementation helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

constexpr bool kImapVerboseLogEnabled = true;
constexpr int  kInitialBodyPeekBytes  = 65536;

// ── Folder-list helpers (used by fetchFolders) ───────────────────────────────

QVariantList
defaultGmailFolders(const QString &accountEmail) {
    QVariantList out;
    static const QList<QPair<QString, QString>> fallback = {
        { "INBOX"_L1,                         "inbox"_L1 },
        { "[Gmail]/All Mail"_L1,              "all"_L1   },
        { "[Gmail]/Sent Mail"_L1,             "sent"_L1  },
        { "[Gmail]/Drafts"_L1,                "drafts"_L1},
        { "[Gmail]/Spam"_L1,                  "junk"_L1  },
        { "[Gmail]/Trash"_L1,                 "trash"_L1 },
        { "[Gmail]/Categories/Primary"_L1,    ""         },
        { "[Gmail]/Categories/Promotions"_L1, ""         },
        { "[Gmail]/Categories/Social"_L1,     ""         },
        { "[Gmail]/Categories/Purchases"_L1,  ""         },
        { "[Gmail]/Categories/Updates"_L1,    ""         },
        { "[Gmail]/Categories/Forums"_L1,     ""         },
    };
    for (const auto &[name, special] : fallback) {
        QVariantMap row;
        row.insert("accountEmail"_L1, accountEmail);
        row.insert("name"_L1, name);
        row.insert("flags"_L1, QString());
        row.insert("specialUse"_L1, special);
        out.push_back(row);
    }
    return out;
}

// ── Fetch-response parsing helpers ───────────────────────────────────────────

QString
parseUidFromFetch(const QString &fetchResp) {
    static const QRegularExpression uidRe(QStringLiteral("UID\\s+(\\d+)"));
    const auto m = uidRe.match(fetchResp);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList
splitFetchResponses(const QString &resp) {
    QStringList parts;
    const QStringList lines = resp.split('\n');
    QString current;
    bool inFetch = false;
    for (const QString &rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith("* "_L1) && trimmed.contains(" FETCH "_L1)) {
            if (inFetch && !current.trimmed().isEmpty()) parts.push_back(current);
            current = rawLine + "\n";
            inFetch = true;
            continue;
        }
        if (inFetch) current += rawLine + "\n";
    }
    if (inFetch && !current.trimmed().isEmpty()) parts.push_back(current);
    return parts;
}

QString
categoryToFolder(const QString &c) {
    const QString k = c.trimmed().toLower();
    if (k == "primary"_L1)    return QStringLiteral("[Gmail]/Categories/Primary");
    if (k == "promotions"_L1) return QStringLiteral("[Gmail]/Categories/Promotions");
    if (k == "social"_L1)     return QStringLiteral("[Gmail]/Categories/Social");
    if (k == "updates"_L1)    return QStringLiteral("[Gmail]/Categories/Updates");
    if (k == "forums"_L1)     return QStringLiteral("[Gmail]/Categories/Forums");
    if (k == "purchases"_L1)  return QStringLiteral("[Gmail]/Categories/Purchases");
    return {};
}

// ── Per-sync state (accumulated across all batch fetches in one sync pass) ───

struct IngestState {
    QVariantList out;
    QSet<QString> seenMessageKeys;

    struct EnrichCandidate { QString uid; QString folder; QString snippetKey; };
    QVector<EnrichCandidate>       snippetEnrichCandidates;
    QSet<QString>                  snippetEnrichSeen;
    QHash<QString, int>            messageIndexByUidFolder;
    QHash<QString, QList<int>>     snippetOutIndexesByKey;
    QStringList                    visibleUids;

    // Gmail category hints: populated by executeFull for Gmail INBOX.
    // Maps UID → set of inferred category folder names.
    QHash<qint32, QSet<QString>>   categoryHints;

    int fetchedCount              = 0;
    int categoryMissCount         = 0;
    int missingAddressHeadersCount = 0;
};

// ── Message ingestion ─────────────────────────────────────────────────────────

void
ingestMessage(const QString &fetchResp, const QString &uid, const QString &debugPrefix, const QString &messageFolder,
                   qint64 rawFetchMs, qint64 enrichMs, qint64 totalFetchMs, const SyncContext &ctx, IngestState &state) {
    const auto headerFieldsBlock = extractHeaderFieldsLiteral(fetchResp.toUtf8());
    const auto &headerSource = headerFieldsBlock.isEmpty() ? fetchResp : headerFieldsBlock;

    QVariantMap h;
    h.insert("uid"_L1, uid);
    h.insert("subject"_L1, decodeRfc2047(extractField(headerSource, "Subject"_L1)));

    const auto fromHeader     = decodeRfc2047(extractField(headerSource, "From"_L1)).trimmed();
    const auto toHeader       = sanitizeAddressHeader(decodeRfc2047(extractField(headerSource, "To"_L1)).trimmed());
    const auto senderHeader   = decodeRfc2047(extractField(headerSource, "Sender"_L1)).trimmed();
    const auto replyToHeader  = decodeRfc2047(extractField(headerSource, "Reply-To"_L1)).trimmed();
    auto returnPathHeader     = decodeRfc2047(extractField(headerSource, "Return-Path"_L1)).trimmed();

    if (returnPathHeader.isEmpty())
        returnPathHeader = decodeRfc2047(extractField(fetchResp, "Return-Path"_L1)).trimmed();

    const auto hasFrom       = !fromHeader.isEmpty();
    const auto hasSender     = !senderHeader.isEmpty();
    const auto hasReplyTo    = !replyToHeader.isEmpty();
    const auto hasReturnPath = !returnPathHeader.isEmpty();

    const auto senderEmail  = extractEmailAddress(fromHeader).trimmed().toLower();
    const auto listIdDomain = extractListIdDomain(headerSource);
    auto bimiLogoUrl        = extractBimiLogoUrl(headerSource);

    const auto senderFallback = !senderHeader.isEmpty()  ? senderHeader :
                                                           !replyToHeader.isEmpty() ? replyToHeader : returnPathHeader;

    h.insert("sender"_L1, normalizeSenderValue(fromHeader, senderFallback));
    h.insert("recipient"_L1, toHeader);
    h.insert("messageIdHeader"_L1, extractField(headerSource, "Message-ID"_L1).trimmed());

    {
        const auto rawUnsub = extractField(headerSource, "List-Unsubscribe"_L1).trimmed();
        if (!rawUnsub.isEmpty()) {
            static const QRegularExpression httpsRe(QStringLiteral("<(https://[^>]+)>"),
                                                    QRegularExpression::CaseInsensitiveOption);

            if (const auto m = httpsRe.match(rawUnsub); m.hasMatch())
                h.insert("listUnsubscribe"_L1, m.captured(1).trimmed());
        }
    }

    // Additional headers for ESP/sender identification and threading.
    // Reply-To and Return-Path are already extracted above for sender-fallback purposes;
    // store them so the UI can use them for vendor detection and reply behaviour.
    if (!replyToHeader.isEmpty())
        h.insert("replyTo"_L1, replyToHeader);
    if (!returnPathHeader.isEmpty())
        h.insert("returnPath"_L1, returnPathHeader);
    {
        const auto v = extractField(headerSource, "Authentication-Results"_L1).trimmed();
        if (!v.isEmpty()) h.insert("authResults"_L1, v);
    }
    {
        const auto v = extractField(headerSource, "X-Mailer"_L1).trimmed();
        if (!v.isEmpty()) h.insert("xMailer"_L1, v);
    }
    {
        const auto v = extractField(headerSource, "In-Reply-To"_L1).trimmed();
        if (!v.isEmpty()) h.insert("inReplyTo"_L1, v);
    }
    {
        const auto v = extractField(headerSource, "References"_L1).trimmed();
        if (!v.isEmpty()) h.insert("references"_L1, v);
    }

    // Derive sending ESP so the UI can show "Mailgun tracking was blocked" instead of the
    // pixel-host domain.  Two signals, tried in order:
    //
    //  1. Definitive X-* headers injected by the ESP — most reliable, but Gmail strips some
    //     (X-Mailgun-Sid in particular) before storing the message in IMAP.
    //  2. Curated Received-chain whitelist — matches only known ESP relay domains
    //     (*.mailgun.org, *.sendgrid.net, …).  This catches the stripped-header case without
    //     the false-positives of arbitrary SLD extraction (which identified Citi, WellsFargo,
    //     Zillow, Capital One's own infrastructure as "ESPs").
    {
        static const struct { const char *marker; const char *vendor; } kEspMarkers[] = {
            { "X-Mailgun-Sid:",        "Mailgun"    },  // always present, even on custom domains
            { "X-SG-EID:",             "Sendgrid"   },  // SendGrid v3
            { "X-SMTPAPI:",            "Sendgrid"   },  // SendGrid legacy SMTP
            { "X-MC-User:",            "Mailchimp"  },  // Mandrill/Mailchimp Transactional
            { "X-Klaviyo-Message-Id:", "Klaviyo"    },
            { "X-PM-Message-Id:",      "Postmark"   },
            { "X-SES-Outgoing:",       "Amazon SES" },
        };

        QString espVendor;
        for (const auto &sig : kEspMarkers) {
            if (headerSource.contains(QLatin1StringView(sig.marker), Qt::CaseInsensitive)) {
                espVendor = QLatin1StringView(sig.vendor);
                break;
            }
        }

        // Received-chain fallback: curated whitelist of known ESP relay domain suffixes.
        // Only matches if no definitive header was found.  We iterate in reverse so the
        // oldest (sending) hop is tested first — that's where the ESP relay appears.
        if (espVendor.isEmpty()) {
            static const struct { const char *suffix; const char *vendor; } kEspRelays[] = {
                { ".mailgun.org",       "Mailgun"    },
                { ".mailgun.net",       "Mailgun"    },
                { ".sendgrid.net",      "Sendgrid"   },
                { ".amazonses.com",     "Amazon SES" },
                { ".psmtp.com",         "Postmark"   },
                { ".postmarkapp.com",   "Postmark"   },
                { ".sparkpostmail.com", "SparkPost"  },
                { ".klaviyoemail.com",  "Klaviyo"    },
                { ".exacttarget.com",   "Salesforce" },
                { ".responsys.net",     "Oracle"     },
                { ".emarsys.net",       "Emarsys"    },
            };
            static const QRegularExpression receivedFromRe(
                QStringLiteral("\\bReceived:\\s+from\\s+(\\S+)"),
                QRegularExpression::CaseInsensitiveOption);

            QStringList relayHosts;
            auto it = receivedFromRe.globalMatch(headerSource);
            while (it.hasNext())
                relayHosts << it.next().captured(1).toLower();

            for (int i = relayHosts.size() - 1; i >= 0 && espVendor.isEmpty(); --i) {
                const QString &host = relayHosts[i];
                for (const auto &relay : kEspRelays) {
                    const auto suffix = QLatin1StringView(relay.suffix); // e.g. ".mailgun.org"
                    const auto bare   = suffix.sliced(1);                // e.g.  "mailgun.org"
                    if (host == bare || host.endsWith(suffix)) {
                        espVendor = QLatin1StringView(relay.vendor);
                        break;
                    }
                }
            }
        }

        if (!espVendor.isEmpty())
            h.insert("espVendor"_L1, espVendor);
    }

    // Sent/draft folders: try to resolve the recipient's avatar.
    const auto folderLower  = messageFolder.toLower();
    const auto sentLikeFolder  = folderLower.contains("/sent"_L1) || folderLower.contains("/draft"_L1);

    if (sentLikeFolder) {
        const auto recipientEmail = extractEmailAddress(toHeader).trimmed().toLower();
        bool allowLookup = true;

        if (!recipientEmail.isEmpty() && ctx.avatarShouldRefresh)
            allowLookup = ctx.avatarShouldRefresh(recipientEmail, 3600, 3);

        if (!recipientEmail.isEmpty() && allowLookup) {
            const auto url = resolveGooglePeopleAvatarUrl(recipientEmail, ctx.cxn->accessToken());
            if (!url.isEmpty()) {
                h.insert("recipientAvatarUrl"_L1,      fetchAvatarBlob(url));
                h.insert("recipientAvatarLookupMiss"_L1, false);
            }
            else {
                h.insert("recipientAvatarLookupMiss"_L1, true);
            }
        }
    }

    {
        static const QRegularExpression gmMsgRe("X-GM-MSGID\\s+(\\d+)"_L1, QRegularExpression::CaseInsensitiveOption);
        h.insert("gmMsgId"_L1, gmMsgRe.match(fetchResp).captured(1).trimmed());
    }

    const auto date = extractField(headerSource, "Date"_L1);
    auto dt = parseBestDateTime(date, fetchResp);
    if (!dt.isValid())
        dt = QDateTime::currentDateTimeUtc();

    h.insert("receivedAt"_L1, dt.toUTC().toString(Qt::ISODate));

    const auto deterministicSnippet = compileDeterministicSnippet(h.value("subject"_L1).toString(), headerSource,
        fetchResp.toUtf8());
    h.insert("snippet"_L1, deterministicSnippet);

    if (!listIdDomain.isEmpty())
        h.insert("avatarDomain"_L1, listIdDomain);

    // Avatar resolution priority: Google People → Gravatar → BIMI header → BIMI DNS → favicon
    QString avatarSource;
    QString avatarUrl;

    bool allowPeopleLookup = true;
    if (!senderEmail.isEmpty() && ctx.avatarShouldRefresh)
        allowPeopleLookup = ctx.avatarShouldRefresh(senderEmail, 3600, 3);

    if (!senderEmail.isEmpty() && allowPeopleLookup) {
        avatarUrl = resolveGooglePeopleAvatarUrl(senderEmail, ctx.cxn->accessToken());
        if (!avatarUrl.isEmpty())
            avatarSource = "google-people"_L1;
    }

    if (avatarUrl.isEmpty() && !senderEmail.isEmpty() && allowPeopleLookup) {
        avatarUrl = resolveGravatarUrl(senderEmail);
        if (!avatarUrl.isEmpty())
            avatarSource = "gravatar"_L1;
    }

    if (avatarUrl.isEmpty()) {
        if (!bimiLogoUrl.isEmpty()) {
            avatarUrl = bimiLogoUrl;
            avatarSource = "bimi-header"_L1;
        }
        else {
            const auto bimiDomain = !listIdDomain.isEmpty() ? listIdDomain : senderDomainFromHeader(fromHeader);
            if (!bimiDomain.isEmpty()) {
                bimiLogoUrl = resolveBimiLogoUrlViaDoh(bimiDomain);

                if (!bimiLogoUrl.isEmpty()) {
                    avatarUrl = bimiLogoUrl;
                    avatarSource = "bimi-dns"_L1;
                }
                else {
                    const auto favIconUrl = resolveFaviconLogoUrl(bimiDomain);
                    if (!favIconUrl.isEmpty()) {
                        avatarUrl = favIconUrl;
                        avatarSource = "favicon"_L1;
                    }
                }
            }
        }
    }

    if (!avatarUrl.isEmpty()) {
        const QString avatarBlob = fetchAvatarBlob(avatarUrl);
        // Google auto-generated monogram/default avatars are tiny (often < 2 KB).
        // Reject them so the UI falls back to initials instead of a generic placeholder.
        const bool tinyGoogleAvatar = avatarSource == "google-people"_L1
                                      && avatarBlob.startsWith("data:"_L1)
                                      && avatarBlob.size() < 3000;
        if (tinyGoogleAvatar) {
            qInfo().noquote() << "[avatar] discarding tiny google-people avatar for" << senderEmail;
        } else {
            h.insert("avatarUrl"_L1,    avatarBlob);
            h.insert("avatarSource"_L1, avatarSource);
        }
    } else if (!listIdDomain.isEmpty()) {
        h.insert("avatarSource"_L1, "listid-domain"_L1);
    }

    // Body HTML is intentionally not persisted during header sync.
    h.insert("bodyHtml"_L1, QString());
    h.insert("folder"_L1,   messageFolder);
    h.insert("unread"_L1,   !fetchResp.contains("\\Seen"_L1, Qt::CaseInsensitive));

    const QString rawLabelsAll = extractGmailLabelsRaw(fetchResp);
    const QString inferredCategoryFolder = ctx.isInbox()
            ? extractGmailCategoryFolder(fetchResp)
            : QString();
    h.insert("rawGmailLabels"_L1, rawLabelsAll);
    if (rawLabelsAll.contains("/Categories/Primary"_L1, Qt::CaseInsensitive))
        h.insert("primaryLabelObserved"_L1, true);

    if (ctx.isInbox()) {
        const QString rawLabels     = rawLabelsAll.left(240);
        const QString gmMsgId       = h.value("gmMsgId"_L1).toString().trimmed();
        const QString msgIdHeader   = h.value("messageIdHeader"_L1).toString();

        qInfo().noquote() << "[imap-category]"
                          << "uid=" << uid
                          << "gmMsgId=" << gmMsgId
                          << "labels=" << rawLabels
                          << "inferred=" << (inferredCategoryFolder.isEmpty()
                                             ? QStringLiteral("<none>") : inferredCategoryFolder)
                          << "subject=" << h.value("subject"_L1).toString().left(120);

        const bool fullAddressCheck = (debugPrefix == "inbox"_L1 || debugPrefix == "inbox-inc"_L1);
        if (fullAddressCheck) {
            qInfo().noquote() << "[imap-address]"
                              << "uid=" << uid
                              << "messageId=" << msgIdHeader.left(120)
                              << "hasFrom=" << hasFrom
                              << "hasSender=" << hasSender
                              << "hasReplyTo=" << hasReplyTo
                              << "hasReturnPath=" << hasReturnPath;

            if (!hasFrom && !hasSender && !hasReplyTo && !hasReturnPath) {
                qWarning().noquote() << "[imap-address-miss-critical]"
                                     << "uid=" << uid
                                     << "messageId=" << msgIdHeader.left(120)
                                     << "subject=" << h.value("subject"_L1).toString().left(120);
                ++state.missingAddressHeadersCount;
            }
        }

        if (inferredCategoryFolder.isEmpty()) {
            qWarning().noquote() << "[imap-category-miss]"
                                 << "uid=" << uid
                                 << "gmMsgId=" << gmMsgId
                                 << "labels=" << rawLabels
                                 << "subject=" << h.value("subject"_L1).toString().left(120);
        }
    }

    if (kImapVerboseLogEnabled) {
        qInfo().noquote() << "----------------------------------------";
        qInfo().noquote() << "[imap-debug]" << debugPrefix
                          << "uid=" << uid
                          << "rawFetchMs=" << rawFetchMs
                          << "enrichMs=" << enrichMs
                          << "fetchMs=" << totalFetchMs
                          << "sender=" << h.value("sender"_L1).toString()
                          << "subject=" << h.value("subject"_L1).toString()
                          << "snippet=" << h.value("snippet"_L1).toString().left(120);
    }

    // commitRow: deduplicate and append to output
    auto commitRow = [&](const QVariantMap &row, const QString &rowFolder) {
        const QString dedupeKey = rowFolder
            + "|" + row.value("sender"_L1).toString()
            + "|" + row.value("subject"_L1).toString()
            + "|" + row.value("receivedAt"_L1).toString();
        if (state.seenMessageKeys.contains(dedupeKey)) return;
        state.seenMessageKeys.insert(dedupeKey);

        const int outIndex = state.out.size();
        state.out.push_back(row);

        const QString idxKey = uid + "|" + rowFolder;
        state.messageIndexByUidFolder.insert(idxKey, outIndex);

        const QString gmMsgId    = row.value("gmMsgId"_L1).toString().trimmed();
        const QString msgIdHdr   = row.value("messageIdHeader"_L1).toString().trimmed().toLower();
        const QString snippetKey = !gmMsgId.isEmpty()   ? ("gm:"_L1 + gmMsgId)
                                 : !msgIdHdr.isEmpty()   ? ("mid:"_L1 + msgIdHdr)
                                                         : ("uid:"_L1 + uid);
        state.snippetOutIndexesByKey[snippetKey].push_back(outIndex);

        if (!state.snippetEnrichSeen.contains(snippetKey)) {
            state.snippetEnrichSeen.insert(snippetKey);
            state.snippetEnrichCandidates.push_back({uid, rowFolder, snippetKey});
        }
        if (ctx.onHeader) ctx.onHeader(row);
    };

    commitRow(h, messageFolder);

    // Route message to its inferred Gmail category folder as a virtual copy.
    if (!inferredCategoryFolder.isEmpty()
            && inferredCategoryFolder.compare(messageFolder, Qt::CaseInsensitive) != 0) {
        QVariantMap categoryRow = h;
        categoryRow.insert("folder"_L1, inferredCategoryFolder);
        commitRow(categoryRow, inferredCategoryFolder);
    }
}

// ── Batch UID fetch ───────────────────────────────────────────────────────────

void
fetchUidBatch(SyncContext &ctx,
                   const std::vector<qint32> &uids,
                   const QString &debugPrefix,
                   IngestState &state,
                   int &throttleBackoffMs) {
    if (uids.empty()) return;
    if (ctx.cancelRequested && ctx.cancelRequested->load()) return;

    std::vector<qint32> requested = uids;
    std::ranges::sort(requested);
    const bool fuzzy = SyncUtils::chunkIsFuzzyContiguous(
        std::span<const qint32>(requested.data(), requested.size()));

    QString uidSpec;
    if (fuzzy) {
        uidSpec = QStringLiteral("%1:%2").arg(requested.front()).arg(requested.back());
    } else {
        QStringList ids;
        ids.reserve(static_cast<int>(requested.size()));
        for (const qint32 u : requested) ids.push_back(QString::number(u));
        uidSpec = ids.join(',');
    }

    const QString fetchCmd = QStringLiteral(
        "UID FETCH %1 (UID FLAGS INTERNALDATE X-GM-LABELS X-GM-MSGID "
        "BODY.PEEK[HEADER.FIELDS (FROM TO SENDER REPLY-TO RETURN-PATH SUBJECT DATE MESSAGE-ID "
        "AUTHENTICATION-RESULTS X-MAILER IN-REPLY-TO REFERENCES RECEIVED "
        "X-MAILGUN-SID X-SG-EID X-SMTPAPI X-MC-USER X-KLAVIYO-MESSAGE-ID X-PM-MESSAGE-ID X-SES-OUTGOING "
        "LIST-ID LIST-UNSUBSCRIBE BIMI-LOCATION LIST-PREVIEW X-PREHEADER X-MC-PREVIEW-TEXT X-ALT-DESCRIPTION)] "
        "BODY.PEEK[]<0.%2>)"
    ).arg(uidSpec).arg(kInitialBodyPeekBytes);

    QElapsedTimer fetchTimer;
    fetchTimer.start();
    const QString batchResp = ctx.cxn->execute(fetchCmd);

    if (!batchResp.contains(" OK"_L1, Qt::CaseInsensitive)) {
        qWarning().noquote() << "[sync-fetch-batch-fail]"
                             << "folder=" << ctx.folderName
                             << "response=" << batchResp.left(220).replace('\n', ' ');
        return;
    }
    if (batchResp.contains("THROTTLED"_L1, Qt::CaseInsensitive)) {
        throttleBackoffMs = (throttleBackoffMs <= 0) ? 250 : qMin(2000, throttleBackoffMs * 2);
        qWarning().noquote() << "[sync-throttle]"
                             << "folder=" << ctx.folderName
                             << "backoffMs=" << throttleBackoffMs;
    } else if (throttleBackoffMs > 0) {
        throttleBackoffMs = qMax(0, throttleBackoffMs - 100);
    }

    const qint64     rawFetchMs     = fetchTimer.elapsed();
    const QStringList fetchParts    = splitFetchResponses(batchResp);
    const int         fetchedParts  = qMax(1, fetchParts.size());
    const qint64      rawFetchMsPerMsg = qMax<qint64>(1, rawFetchMs / fetchedParts);

    for (const QString &part : fetchParts) {
        const QString uid = parseUidFromFetch(part);
        if (uid.isEmpty()) continue;

        if (fuzzy) {
            bool ok = false;
            const qint32 uidV = uid.toInt(&ok);
            if (!ok || !std::ranges::binary_search(requested, uidV)) continue;
        }

        // Inject synthetic X-GM-LABELS for Gmail INBOX category hints if the server
        // didn't return them directly in this FETCH response.
        QString fetchResp = part;
        if (!state.categoryHints.isEmpty()) {
            bool ok = false;
            const qint32 uidV = uid.toInt(&ok);
            if (ok) {
                const QSet<QString> hinted = state.categoryHints.value(uidV);
                if (!hinted.isEmpty() && extractGmailCategoryFolder(fetchResp).isEmpty()) {
                    QStringList sortedHints = hinted.values();
                    std::ranges::sort(sortedHints);
                    QStringList quoted;
                    for (const QString &hf : sortedHints)
                        quoted.push_back(QStringLiteral("\"%1\"").arg(hf));
                    fetchResp += QStringLiteral("\nX-GM-LABELS (%1)\n").arg(quoted.join(' '));
                }
            }
        }

        QElapsedTimer enrichTimer;
        enrichTimer.start();
        const qint64 enrichMs = enrichTimer.elapsed();

        ++state.fetchedCount;
        state.visibleUids.push_back(uid);
        if (extractGmailCategoryFolder(fetchResp).isEmpty()) ++state.categoryMissCount;

        ingestMessage(fetchResp, uid, debugPrefix, ctx.cxn->selectedFolder(),
                      rawFetchMsPerMsg, enrichMs, rawFetchMsPerMsg + enrichMs, ctx, state);
    }
}

// ── Snippet enrichment (Phases 1–3 + final sweep) ────────────────────────────

void
enrichSnippets(const SyncContext &ctx, IngestState &state) {
    // Phases 1 and 2 (header/body enrichment) and Phase 3 (BODYSTRUCTURE + part
    // previews) are currently disabled via their respective limit constants.
    // They can be re-enabled here when limits are raised; the data structures in
    // IngestState already support them (snippetEnrichCandidates, etc.).

    Q_UNUSED(ctx);

    // Absolute final sweep: clear low-quality snippet remnants.
    for (int i = 0; i < state.out.size(); ++i) {
        QVariantMap row = state.out.at(i).toMap();
        const QString sn = row.value("snippet"_L1).toString().trimmed();
        if (sn.startsWith("From "_L1, Qt::CaseInsensitive)
                || snippetLooksLikeProtocolOrJunk(sn)) {
            row.insert("snippet"_L1, QString());
            state.out[i] = row;
        }
    }
}

// ── Incremental sync ──────────────────────────────────────────────────────────

SyncResult
executeIncremental(SyncContext &ctx) {
    SyncResult result;
    IngestState state;
    KestrelTimer folderSearchTimer;

    // Search for new UIDs above minUidExclusive.
    QStringList newUids;
    if (ctx.isGmailInbox()) {
        // Gmail INBOX: search per-category to collect category hints up front.
        static const QStringList inboxCategories = {
            "primary"_L1, "promotions"_L1, "social"_L1,
            "updates"_L1, "forums"_L1, "purchases"_L1
        };
        QSet<QString> merged;
        for (const auto &cat : inboxCategories) {
            const auto sCmd = QStringLiteral("UID SEARCH UID %1:* X-GM-RAW \"category:%2\"").arg(ctx.minUidExclusive + 1).arg(cat);
            const auto sResp = ctx.cxn->execute(sCmd);
            const auto mappedFolder = categoryToFolder(cat);
            for (const auto &u : parseSearchIds(sResp)) {
                merged.insert(u);
                // Build per-UID category hints — mirrors the full-sync path so that
                // messages whose X-GM-LABELS don't include the category label yet
                // (race between delivery and Gmail's label application) still get routed.
                if (!mappedFolder.isEmpty()) {
                    bool ok = false;
                    const qint32 v = u.toInt(&ok);
                    if (ok && v > 0) state.categoryHints[v].insert(mappedFolder);
                }
            }
        }
        newUids = merged.values();
    } else {
        const auto sCmd = QStringLiteral("UID SEARCH UID %1:*").arg(ctx.minUidExclusive + 1);
        const auto sResp = ctx.cxn->execute(sCmd);
        newUids = parseSearchIds(sResp);
    }

    // Filter: only UIDs strictly greater than minUidExclusive.
    const auto rawReturned = newUids.size();
    QStringList filtered;
    filtered.reserve(newUids.size());
    for (const auto &u : newUids) {
        bool ok = false;
        if (const auto v = u.toLongLong(&ok); ok && v > ctx.minUidExclusive)
            filtered.push_back(u);
    }
    newUids = filtered;
    std::ranges::sort(newUids, [](const QString &a, const QString &b) {
        return a.toLongLong() < b.toLongLong();
    });

    qInfo().noquote() << "[sync-folder-search]"
                      << "foldername=" << ctx.folderName
                      << "minUidExclusive=" << ctx.minUidExclusive
                      << "rawReturned=" << rawReturned
                      << "filteredReturned=" << newUids.size()
                      << "firstUid=" << (newUids.isEmpty() ? QString() : newUids.first())
                      << "lastUid="  << (newUids.isEmpty() ? QString() : newUids.last())
                      << "elapsedMs=" << folderSearchTimer.elapsed();

    // Fetch new UIDs in batches.
    qint32 throttleBackoffMs = 0;
    using SyncUtils::kSyncBatchSize;
    std::array<qint32, kSyncBatchSize> incBatch{};
    int incN = 0;

    for (const auto &uid : newUids) {
        bool ok = false;
        const qint32 v = uid.toInt(&ok);

        if (!ok)
            continue;

        incBatch[incN++] = v;
        if (incN == kSyncBatchSize) {
            if (throttleBackoffMs > 0)
                QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));

            fetchUidBatch(ctx, std::vector(incBatch.begin(), incBatch.begin() + incN),
                          "inbox-inc"_L1, state, throttleBackoffMs);
            incN = 0;
        }
    }

    if (incN > 0) {
        if (throttleBackoffMs > 0)
            QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));

        fetchUidBatch(ctx, std::vector(incBatch.begin(), incBatch.begin() + incN),
                      "inbox-inc"_L1, state, throttleBackoffMs);
    }

    // For non-INBOX folders: backfill messages within budget that aren't cached locally.
    if (!ctx.isInbox()) {
        if (ctx.fetchBudget != 0 && ctx.getFolderUids) {
            const auto localUids = ctx.getFolderUids(ctx.cxn->email(), ctx.folderName);
            const QSet localSet(localUids.begin(), localUids.end());
            const QSet newSet(newUids.begin(), newUids.end());

            const auto allResp = ctx.cxn->execute(QStringLiteral("UID SEARCH ALL"));
            QStringList allUids = parseUidSearchAll(allResp);

            std::ranges::sort(allUids, [](const QString &a, const QString &b) {
                return a.toLongLong() < b.toLongLong();
            });

            int fetchedBackfill = 0;
            std::array<qint32, kSyncBatchSize> backfillBatch{};
            int backfillN = 0;

            auto flushBackfill = [&]() {
                if (backfillN <= 0) return;

                if (throttleBackoffMs > 0)
                    QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));

                fetchUidBatch(ctx,
                              std::vector<qint32>(backfillBatch.begin(), backfillBatch.begin() + backfillN),
                              "folder-backfill"_L1, state, throttleBackoffMs);

                backfillN = 0;
            };

            for (auto i = allUids.size() - 1; i >= 0 && (ctx.fetchBudget < 0 || fetchedBackfill < ctx.fetchBudget); --i) {
                const auto uid = allUids.at(i);
                if (newSet.contains(uid) || localSet.contains(uid))
                    continue;

                bool ok = false;
                const qint32 uidV = uid.toInt(&ok);

                if (!ok) continue;

                backfillBatch[backfillN++] = uidV;
                ++fetchedBackfill;

                if (backfillN == kSyncBatchSize)
                    flushBackfill();
            }

            flushBackfill();

            qInfo().noquote() << "[sync-folder-backfill]"
                              << "folder=" << ctx.folderName
                              << "budget=" << ctx.fetchBudget
                              << "newUids=" << newUids.size()
                              << "localKnown=" << localSet.size()
                              << "fetchedBackfill=" << fetchedBackfill;
        }
    }

    enrichSnippets(ctx, state);

    result.success                     = true;
    result.headers                     = state.out;
    result.fetchedCount                = state.fetchedCount;
    result.categoryMissCount           = state.categoryMissCount;
    result.missingAddressHeadersCount  = state.missingAddressHeadersCount;
    result.statusMessage               = QStringLiteral("Fetched %1 headers.").arg(state.out.size());
    return result;
}

// ── Full sync ─────────────────────────────────────────────────────────────────

SyncResult
executeFull(SyncContext &ctx) {
    SyncResult result;
    IngestState state;
    KestrelTimer searchAllTimer;

    // Load locally known UIDs to skip them during this fetch.
    QSet<qint32> knownFolderUids;
    if (ctx.getFolderUids) {
        KestrelTimer dedupeTimer;
        const QStringList local = ctx.getFolderUids(ctx.cxn->email(), ctx.cxn->selectedFolder());
        for (const QString &u : local) {
            bool ok = false;
            const qint32 v = u.toInt(&ok);
            if (ok && v > 0) knownFolderUids.insert(v);
        }
        qInfo().noquote() << "[sync-initial-dedupe]"
                          << "folder=" << ctx.cxn->selectedFolder()
                          << "knownUids=" << knownFolderUids.size()
                          << "elapsedMs=" << dedupeTimer.elapsed();
    }

    QSet<qint32> pendingUidSet;
    int fetchedTotal      = 0;
    int throttleBackoffMs = 0;

    // UID SEARCH ALL to collect all remote UIDs.
    const QString allResp = ctx.cxn->execute(QStringLiteral("UID SEARCH ALL"));
    const QStringList allIds = parseSearchIds(allResp);
    qInfo().noquote() << "[sync-search-all-result]"
                      << "folder=" << ctx.folderName
                      << "count=" << allIds.size()
                      << "elapsedMs=" << searchAllTimer.elapsed();

    searchAllTimer.restart();
    for (const QString &id : allIds) {
        bool ok = false;
        const qint32 v = id.toInt(&ok);
        if (ok && v > 0 && !knownFolderUids.contains(v)) pendingUidSet.insert(v);
    }

    // Gmail INBOX: per-category searches to build category hints.
    if (ctx.isGmailInbox()) {
        static const QStringList inboxCategories = {
            "primary"_L1, "promotions"_L1, "social"_L1,
            "updates"_L1, "forums"_L1, "purchases"_L1
        };
        for (const QString &cat : inboxCategories) {
            const QString cmd = QStringLiteral("UID SEARCH X-GM-RAW \"category:%1\"").arg(cat);
            const QString resp = ctx.cxn->execute(cmd);
            const QStringList ids = parseSearchIds(resp);
            qInfo().noquote() << "[sync-search-category-result]"
                              << "category=" << cat << "count=" << ids.size() << "elapsedMs=" << searchAllTimer.elapsed();
            searchAllTimer.restart();
            const QString mappedFolder = categoryToFolder(cat);
            for (const QString &id : ids) {
                bool ok = false;
                const qint32 v = id.toInt(&ok);
                if (ok && v > 0 && !mappedFolder.isEmpty())
                    state.categoryHints[v].insert(mappedFolder);
            }
        }
    }

    searchAllTimer.restart();

    // Flush pending UIDs in budget-capped batches (newest first).
    using SyncUtils::kSyncBatchSize;
    auto flushPending = [&]() {
        if (pendingUidSet.isEmpty())
            return;

        const bool unlimited = (ctx.fetchBudget < 0);
        const int remainingBudget = unlimited ? INT_MAX : qMax(0, ctx.fetchBudget - fetchedTotal);
        if (remainingBudget <= 0) {
            pendingUidSet.clear(); return;
        }

        std::vector<qint32> pending;
        pending.reserve(pendingUidSet.size());
        for (const qint32 v : pendingUidSet) pending.push_back(v);
        std::ranges::sort(pending, std::greater<qint32>());
        if (!unlimited && pending.size() > static_cast<size_t>(remainingBudget))
            pending.resize(static_cast<size_t>(remainingBudget));

        int idx = 0;
        while (idx < static_cast<int>(pending.size())) {
            std::vector<qint32> chunk;
            chunk.reserve(kSyncBatchSize);
            while (idx < static_cast<int>(pending.size())
                   && static_cast<int>(chunk.size()) < kSyncBatchSize)
                chunk.push_back(pending[idx++]);

            qInfo().noquote() << "[sync-fetch-batch]"
                              << "folder=" << ctx.folderName
                              << "size=" << chunk.size()
                              << "fuzzyContiguous=" << SyncUtils::chunkIsFuzzyContiguous(
                                     std::span<const qint32>(chunk.data(), chunk.size()))
                              << "elapsedMs=" << searchAllTimer.elapsed();

            if (throttleBackoffMs > 0)
                QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));
            fetchUidBatch(ctx, chunk, "inbox"_L1, state, throttleBackoffMs);
        }
        fetchedTotal += static_cast<int>(pending.size());
        pendingUidSet.clear();
    };

    flushPending();
    enrichSnippets(ctx, state);

    result.success                    = true;
    result.headers                    = state.out;
    result.fetchedCount               = state.fetchedCount;
    result.categoryMissCount          = state.categoryMissCount;
    result.missingAddressHeadersCount = state.missingAddressHeadersCount;
    result.statusMessage              = QStringLiteral("Fetched %1 headers.").arg(state.out.size());
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// FolderSync public API
// ─────────────────────────────────────────────────────────────────────────────

QVariantList
FolderSync::fetchFolders(const QString &host, const qint32 port,
                         const QString &email, const QString &accessToken,
                         QString *statusOut) {
    if (accessToken.isEmpty()) {
        if (statusOut) *statusOut = "No access token for folder fetch.";
        return {};
    }

    auto cxn = std::make_shared<Connection>();
    const auto connectResult = cxn->connectAndAuth(host, port, email, accessToken);
    if (!connectResult.success) {
        if (statusOut) *statusOut = "Using default Gmail folders.";
        return defaultGmailFolders(email);
    }

    QVariantList out = cxn->list();
    for (auto &entry : out) {
        auto row = entry.toMap();
        row.insert("accountEmail"_L1, email);
        entry = row;
    }

    if (out.isEmpty())
        out = defaultGmailFolders(email);

    cxn->disconnect();

    if (statusOut) *statusOut = QStringLiteral("Fetched %1 folders.").arg(out.size());
    return out;
}

SyncResult
FolderSync::execute(SyncContext &ctx) {
    KestrelTimer flagsReconcileTimer;

    if (!ctx.cxn) {
        SyncResult r;
        r.success = false;
        r.statusMessage = QStringLiteral("No connection provided");
        return r;
    }

    // SELECT the target folder, with [Gmail]/[Google Mail] alias fallback.
    auto [selected, selResp] = ctx.cxn->select(ctx.folderName);
    if (!selected) {
        QStringList aliases;
        const QString folderLower = ctx.folderName.toLower();
        if (folderLower.startsWith("[gmail]"_L1)) {
            QString alt = ctx.folderName;
            alt.replace("[Gmail]"_L1, "[Google Mail]"_L1, Qt::CaseInsensitive);
            aliases << alt;
        } else if (folderLower.startsWith("[google mail]"_L1)) {
            QString alt = ctx.folderName;
            alt.replace("[Google Mail]"_L1, "[Gmail]"_L1, Qt::CaseInsensitive);
            aliases << alt;
        }

        bool selected = false;
        for (const QString &alt : aliases) {
            std::tie(selected, selResp) = ctx.cxn->select(alt);
            if (selected) {
                break;
            }
        }
        if (!selected) {
            SyncResult r;
            r.success = false;
            r.statusMessage = QStringLiteral("SELECT failed for folder: %1").arg(ctx.folderName);
            return r;
        }
    }

    // Delete reconciliation: compare remote UID set to local and purge deleted UIDs.
    // Runs only when explicitly requested (non-announce incremental INBOX syncs).
    if (ctx.reconcileDeletes && ctx.isInbox() && ctx.getFolderUids && ctx.removeUids) {
        const QString allResp = ctx.cxn->execute(QStringLiteral("UID SEARCH ALL"));
        const QStringList remoteUids = parseUidSearchAll(allResp);

        const QStringList localUids  = ctx.getFolderUids(ctx.cxn->email(), ctx.folderName);
        const QSet<QString> remoteSet(remoteUids.begin(), remoteUids.end());
        const QSet<QString> localSet(localUids.begin(), localUids.end());
        const QStringList deleted    = (localSet - remoteSet).values();
        if (!deleted.isEmpty()) ctx.removeUids(ctx.cxn->email(), deleted);
    }

    // FLAGS reconciliation: for incremental syncs, search for SEEN UIDs in a recent window
    // to pick up \Seen changes from other clients (e.g. phone or webmail).
    // We only mark messages read — never unread — so this never overrides local state.
    if (ctx.minUidExclusive > 0 && ctx.onFlagsReconciled) {
        const qint64 windowStart = qMax(qint64(1), ctx.minUidExclusive - 499);
        const QString flagResp = ctx.cxn->execute(
            QStringLiteral("UID SEARCH UID %1:%2 SEEN")
                .arg(windowStart).arg(ctx.minUidExclusive));
        const QStringList seenUids = parseSearchIds(flagResp);
        if (!seenUids.isEmpty()) {
            ctx.onFlagsReconciled(ctx.cxn->email(), ctx.cxn->selectedFolder(), seenUids);
            qInfo().noquote() << "[flags-reconcile]"
                              << "folder=" << ctx.cxn->selectedFolder()
                              << "window=" << windowStart << ":" << ctx.minUidExclusive
                              << "seenCount=" << seenUids.size()
                              << "elapsedMs=" << flagsReconcileTimer.elapsed();
        }
    }

    if (ctx.minUidExclusive > 0)
        return executeIncremental(ctx);
    else
        return executeFull(ctx);
}

} // namespace Imap
