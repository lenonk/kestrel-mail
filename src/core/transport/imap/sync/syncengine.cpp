#include "syncengine.h"
#include "syncutils.h"
#include "kestreltimer.h"
#include "../parser/responseparser.h"
#include "../connection/imapconnection.h"
#include "../message/messageutils.h"
#include "../message/avatarresolver.h"
#include "../message/bodyprocessor.h"
#include "../../../utils.h"

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
#include <ranges>
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
using Imap::MessageUtils::decodeRfc2047;
using Imap::MessageUtils::compileDeterministicSnippet;

// Address utilities
using Imap::MessageUtils::extractEmailAddress;
using Imap::MessageUtils::normalizeSenderValue;
using Imap::MessageUtils::sanitizeAddressHeader;
using Imap::AvatarResolver::extractListIdDomain;

// Avatar utilities
using Imap::AvatarResolver::extractBimiLogoUrl;
using Imap::AvatarResolver::senderDomainFromHeader;
using Imap::AvatarResolver::resolveBimiLogoUrlViaDoh;
using Imap::AvatarResolver::resolveFaviconLogoUrl;
using Imap::AvatarResolver::resolveGooglePeopleAvatarUrl;
using Imap::AvatarResolver::resolveGravatarUrl;
using Imap::AvatarResolver::fetchAvatarBlob;
using Imap::AvatarResolver::writeAvatarFile;

// Body processor
using Imap::BodyProcessor::parseAttachmentParts;

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
    static const QRegularExpression uidRe("UID\\s+(\\d+)"_L1);
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
    if (k == "primary"_L1)    return "[Gmail]/Categories/Primary"_L1;
    if (k == "promotions"_L1) return "[Gmail]/Categories/Promotions"_L1;
    if (k == "social"_L1)     return "[Gmail]/Categories/Social"_L1;
    if (k == "updates"_L1)    return "[Gmail]/Categories/Updates"_L1;
    if (k == "forums"_L1)     return "[Gmail]/Categories/Forums"_L1;
    if (k == "purchases"_L1)  return "[Gmail]/Categories/Purchases"_L1;
    return {};
}

qint64
parseHighestModSeq(const QString &resp) {
    static const QRegularExpression re("\\[HIGHESTMODSEQ\\s+(\\d+)\\]"_L1,
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(resp);
    if (!m.hasMatch()) return 0;
    bool ok = false;
    const qint64 v = m.captured(1).toLongLong(&ok);
    return ok ? v : 0;
}

qint64
parseExistsFromSelectResponse(const QString &resp) {
    static const QRegularExpression existsRe("\\*\\s+(\\d+)\\s+EXISTS"_L1,
                                             QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = existsRe.match(resp);
    if (!m.hasMatch())
        return -1;
    bool ok = false;
    const qint64 v = m.captured(1).toLongLong(&ok);
    return ok ? v : -1;
}

qint64
parseMessagesFromStatusResponse(const QString &resp) {
    static const QRegularExpression messagesRe("\\bMESSAGES\\s+(\\d+)\\b"_L1,
                                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = messagesRe.match(resp);
    if (!m.hasMatch())
        return -1;
    bool ok = false;
    const qint64 v = m.captured(1).toLongLong(&ok);
    return ok ? v : -1;
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

// Populate categoryHints via X-GM-RAW "category:..." searches for UIDs in [minUid, maxUid].
// Used by both executeFull and executeIncremental as a reliable fallback when X-GM-LABELS
// in the FETCH response does not include the category (common on initial bulk fetches).
void
buildCategoryHints(SyncContext &ctx, IngestState &state, qint64 minUid, qint64 maxUid) {
    if (!ctx.isGmailInbox()) return;
    static const QStringList kCategories = {
        "primary"_L1, "promotions"_L1, "social"_L1,
        "updates"_L1, "forums"_L1, "purchases"_L1
    };
    const QString uidRange = QStringLiteral("%1:%2").arg(minUid).arg(maxUid);
    for (const auto &cat : kCategories) {
        const auto hCmd = "UID SEARCH UID %1 X-GM-RAW \"category:%2\""_L1
                              .arg(uidRange).arg(cat);
        const auto hResp = ctx.cxn->execute(hCmd);
        const auto mappedFolder = categoryToFolder(cat);
        if (mappedFolder.isEmpty()) continue;
        for (const auto &u : parseSearchIds(hResp)) {
            bool ok = false;
            const qint32 v = u.toInt(&ok);
            if (ok && v > 0) state.categoryHints[v].insert(mappedFolder);
        }
    }
}

// ── Message ingestion helpers ─────────────────────────────────────────────────

// Resolve the best sender email using the From → Sender → Reply-To fallback chain.
// Avoids Return-Path because it is often a VERP/bounce address from an ESP, not the brand.
QString
resolveSenderEmail(const QString &fromHeader, const QString &senderHeader, const QString &replyToHeader) {
    auto e = Kestrel::normalizeEmail(extractEmailAddress(fromHeader));
    if (!e.isEmpty()) {
        return e;
    }
    e = Kestrel::normalizeEmail(extractEmailAddress(senderHeader));
    if (!e.isEmpty()) {
        return e;
    }
    return Kestrel::normalizeEmail(extractEmailAddress(replyToHeader));
}

// Detect the sending ESP (e.g. Mailgun, Sendgrid) from headers or Received-chain relay hosts.
// Returns an empty string when no known ESP is identified.
//
// Two signals, tried in order:
//  1. Definitive X-* headers injected by the ESP -- most reliable, but Gmail strips some
//     (X-Mailgun-Sid in particular) before storing the message in IMAP.
//  2. Curated Received-chain whitelist -- matches only known ESP relay domains
//     (*.mailgun.org, *.sendgrid.net, ...).  This catches the stripped-header case without
//     the false-positives of arbitrary SLD extraction (which identified Citi, WellsFargo,
//     Zillow, Capital One's own infrastructure as "ESPs").
QString
detectEspVendor(const QString &headerSource) {
    static const struct { const char *marker; const char *vendor; } kEspMarkers[] = {
        { "X-Mailgun-Sid:",        "Mailgun"    },  // always present, even on custom domains
        { "X-SG-EID:",             "Sendgrid"   },  // SendGrid v3
        { "X-SMTPAPI:",            "Sendgrid"   },  // SendGrid legacy SMTP
        { "X-MC-User:",            "Mailchimp"  },  // Mandrill/Mailchimp Transactional
        { "X-Klaviyo-Message-Id:", "Klaviyo"    },
        { "X-PM-Message-Id:",      "Postmark"   },
        { "X-SES-Outgoing:",       "Amazon SES" },
    };

    if (auto it = std::ranges::find_if(kEspMarkers, [&](const auto &sig) {
            return headerSource.contains(QLatin1StringView(sig.marker), Qt::CaseInsensitive);
        }); it != std::end(kEspMarkers)) {
        return QLatin1StringView(it->vendor);
    }

    // Received-chain fallback: curated whitelist of known ESP relay domain suffixes.
    // We iterate in reverse so the oldest (sending) hop is tested first -- that's where
    // the ESP relay appears.
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
        "\\bReceived:\\s+from\\s+(\\S+)"_L1,
        QRegularExpression::CaseInsensitiveOption);

    QStringList relayHosts;
    auto it = receivedFromRe.globalMatch(headerSource);
    while (it.hasNext()) {
        relayHosts << it.next().captured(1).toLower();
    }

    for (int i = relayHosts.size() - 1; i >= 0; --i) {
        const QString &host = relayHosts[i];
        for (const auto &relay : kEspRelays) {
            const auto suffix = QLatin1StringView(relay.suffix); // e.g. ".mailgun.org"
            const auto bare   = suffix.sliced(1);                // e.g.  "mailgun.org"
            if (host == bare || host.endsWith(suffix)) {
                return QLatin1StringView(relay.vendor);
            }
        }
    }

    return {};
}

// Resolve the sender's avatar through the priority chain:
// Google People -> Gravatar -> BIMI header -> BIMI DNS -> favicon.
// Inserts "avatarUrl" and "avatarSource" into |h| when resolved.
void
resolveAvatarForMessage(QVariantMap &h, const QString &senderEmail, const QString &listIdDomain,
                        QString bimiLogoUrl, const QString &accessToken) {
    QString avatarUrl;
    QString avatarSource;
    if (!senderEmail.isEmpty()) {
        avatarUrl = resolveGooglePeopleAvatarUrl(senderEmail, accessToken);
        if (!avatarUrl.isEmpty()) {
            avatarSource = "google-people"_L1;
        }
    }

    if (avatarUrl.isEmpty() && !senderEmail.isEmpty()) {
        avatarUrl = resolveGravatarUrl(senderEmail);
        if (!avatarUrl.isEmpty()) {
            avatarSource = "gravatar"_L1;
        }
    }

    if (avatarUrl.isEmpty()) {
        if (!bimiLogoUrl.isEmpty()) {
            avatarUrl = bimiLogoUrl;
            avatarSource = "bimi-header"_L1;
        }
        else {
            const auto bimiDomain = !listIdDomain.isEmpty() ? listIdDomain : senderDomainFromHeader(senderEmail);
            if (!bimiDomain.isEmpty()) {
                bimiLogoUrl = resolveBimiLogoUrlViaDoh(bimiDomain);

                if (!bimiLogoUrl.isEmpty()) {
                    avatarUrl = bimiLogoUrl;
                    avatarSource = "bimi-dns"_L1;
                }
                else {
                    if (const auto favIconUrl = resolveFaviconLogoUrl(bimiDomain); !favIconUrl.isEmpty()) {
                        avatarUrl = favIconUrl;
                        avatarSource = "favicon"_L1;
                    }
                }
            }
        }
    }

    if (!avatarUrl.isEmpty()) {
        // Pass the access token for Google People photo URLs -- contact photos at
        // lh3.googleusercontent.com/contacts/ require auth; without it the fetch
        // returns HTML and the raw URL would be stored, then blocked by the UI guardrail.
        const QString token = (avatarSource == "google-people"_L1) ? accessToken : QString{};
        const QString avatarBlob = fetchAvatarBlob(avatarUrl, token);
        // Google auto-generated monogram/default avatars are tiny (often < 2 KB).
        // Reject them so the UI falls back to initials instead of a generic placeholder.
        const bool tinyGoogleAvatar = avatarSource == "google-people"_L1
                                      && avatarBlob.startsWith("data:"_L1)
                                      && avatarBlob.size() < 3000;
        // Write data URI to disk on the worker thread so upsertHeader never does file I/O
        // on the main thread. Pass the file:// URL to the store instead of the blob.
        const bool resolvedToDataUri = avatarBlob.startsWith("data:"_L1);
        if (tinyGoogleAvatar) {
            qInfo().noquote() << "[avatar] discarding tiny google-people avatar for" << senderEmail;
        } else if (resolvedToDataUri) {
            const QString fileUrl = writeAvatarFile(senderEmail, avatarBlob);
            if (!fileUrl.isEmpty()) {
                h.insert("avatarUrl"_L1,    fileUrl);
                h.insert("avatarSource"_L1, avatarSource);
            }
        } else if (avatarSource != "google-people"_L1) {
            // Defensive: non-Google sources always return data URIs; this branch shouldn't fire.
            h.insert("avatarUrl"_L1,    avatarBlob);
            h.insert("avatarSource"_L1, avatarSource);
        }
    } else if (!listIdDomain.isEmpty()) {
        h.insert("avatarSource"_L1, "listid-domain"_L1);
    }
}

// ── Message ingestion ─────────────────────────────────────────────────────────

void
ingestMessage(const QString &fetchResp, const QString &uid, const QString &debugPrefix, const QString &messageFolder,
                   const SyncContext &ctx, IngestState &state) {
    const auto headerFieldsBlock = extractHeaderFieldsLiteral(fetchResp.toUtf8());
    const auto &headerSource = headerFieldsBlock.isEmpty() ? fetchResp : headerFieldsBlock;

    QVariantMap h;
    h.insert("uid"_L1, uid);
    h.insert("subject"_L1, decodeRfc2047(extractField(headerSource, "Subject"_L1)));

    const auto fromHeader     = decodeRfc2047(extractField(headerSource, "From"_L1)).trimmed();
    const auto toHeader       = sanitizeAddressHeader(decodeRfc2047(extractField(headerSource, "To"_L1)).trimmed());
    const auto ccHeader       = sanitizeAddressHeader(decodeRfc2047(extractField(headerSource, "Cc"_L1)).trimmed());
    const auto senderHeader   = decodeRfc2047(extractField(headerSource, "Sender"_L1)).trimmed();
    const auto replyToHeader  = decodeRfc2047(extractField(headerSource, "Reply-To"_L1)).trimmed();
    auto returnPathHeader     = decodeRfc2047(extractField(headerSource, "Return-Path"_L1)).trimmed();

    if (returnPathHeader.isEmpty())
        returnPathHeader = decodeRfc2047(extractField(fetchResp, "Return-Path"_L1)).trimmed();

    const auto hasFrom       = !fromHeader.isEmpty();
    const auto hasSender     = !senderHeader.isEmpty();
    const auto hasReplyTo    = !replyToHeader.isEmpty();
    const auto hasReturnPath = !returnPathHeader.isEmpty();

    const auto senderEmail = resolveSenderEmail(fromHeader, senderHeader, replyToHeader);

    const auto listIdDomain = extractListIdDomain(headerSource);
    const auto bimiLogoUrl  = extractBimiLogoUrl(headerSource);

    // Prefer fallback headers that contain an extractable email so the
    // sender column always carries an address when one exists anywhere.
    auto hasEmail = [](const QString &h) { return !h.isEmpty() && !extractEmailAddress(h).isEmpty(); };
    const auto senderFallback = hasEmail(senderHeader)    ? senderHeader
                              : hasEmail(replyToHeader)    ? replyToHeader
                              : hasEmail(returnPathHeader) ? returnPathHeader
                              : !senderHeader.isEmpty()    ? senderHeader
                              : !replyToHeader.isEmpty()   ? replyToHeader
                              :                              returnPathHeader;

    h.insert("sender"_L1, normalizeSenderValue(fromHeader, senderFallback));
    h.insert("recipient"_L1, toHeader);
    if (!ccHeader.isEmpty())
        h.insert("cc"_L1, ccHeader);
    h.insert("messageIdHeader"_L1, extractField(headerSource, "Message-ID"_L1).trimmed());

    {
        const auto rawUnsub = extractField(headerSource, "List-Unsubscribe"_L1).trimmed();
        if (!rawUnsub.isEmpty()) {
            static const QRegularExpression httpsRe("<(https://[^>]+)>"_L1,
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

    {
        const auto espVendor = detectEspVendor(headerSource);
        if (!espVendor.isEmpty()) {
            h.insert("espVendor"_L1, espVendor);
        }
    }

    // Sent/draft folders: try to resolve the recipient's avatar.
    const auto folderLower  = messageFolder.toLower();
    const auto sentLikeFolder  = folderLower.contains("/sent"_L1) || folderLower.contains("/draft"_L1);

    if (sentLikeFolder) {
        const auto recipientEmail = Kestrel::normalizeEmail(extractEmailAddress(toHeader));
        bool allowLookup = true;

        if (!recipientEmail.isEmpty() && ctx.avatarShouldRefresh)
            allowLookup = ctx.avatarShouldRefresh(recipientEmail, 3600, 3);

        if (!recipientEmail.isEmpty() && allowLookup) {
            const auto url = resolveGooglePeopleAvatarUrl(recipientEmail, ctx.cxn->accessToken());
            if (!url.isEmpty()) {
                const QString blob = fetchAvatarBlob(url);
                const QString fileUrl = blob.startsWith("data:"_L1)
                                        ? writeAvatarFile(recipientEmail, blob)
                                        : blob;
                h.insert("recipientAvatarUrl"_L1,        fileUrl);
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
    {
        static const QRegularExpression gmThrRe("X-GM-THRID\\s+(\\d+)"_L1, QRegularExpression::CaseInsensitiveOption);
        h.insert("gmThrId"_L1, gmThrRe.match(fetchResp).captured(1).trimmed());
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

    // Avatar resolution priority: Google People -> Gravatar -> BIMI header -> BIMI DNS -> favicon.
    // avatarShouldRefresh returns false for file:// entries (already resolved); gate the
    // entire pipeline so we skip expensive BIMI/favicon fetches on already-resolved contacts.
    {
        bool allowAvatarLookup = true;
        if (!senderEmail.isEmpty() && ctx.avatarShouldRefresh) {
            allowAvatarLookup = ctx.avatarShouldRefresh(senderEmail, 3600, 3);
        }
        if (allowAvatarLookup) {
            resolveAvatarForMessage(h, senderEmail, listIdDomain, bimiLogoUrl, ctx.cxn->accessToken());
        }
    }

    // Body HTML is intentionally not persisted during header sync.
    h.insert("bodyHtml"_L1, QString());
    h.insert("folder"_L1,   messageFolder);
    h.insert("unread"_L1,   !fetchResp.contains("\\Seen"_L1, Qt::CaseInsensitive));
    h.insert("flagged"_L1,  fetchResp.contains("\\Flagged"_L1, Qt::CaseInsensitive) ? 1 : 0);

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


        const bool fullAddressCheck = (debugPrefix == "inbox"_L1 || debugPrefix == "inbox-inc"_L1);
        if (fullAddressCheck) {

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
    }

    // Attachment metadata from BODYSTRUCTURE — stored in DB so the UI needs no IMAP round-trip.
    {
        const QList<Imap::BodyProcessor::BodyPart> attParts = parseAttachmentParts(fetchResp);
        if (!attParts.isEmpty()) {
            QVariantList attachments;
            attachments.reserve(attParts.size());
            for (const auto &p : attParts) {
                QVariantMap row;
                row.insert("partId"_L1,       p.partId);
                row.insert("name"_L1,         p.filename.isEmpty() ? "Attachment"_L1 : p.filename);
                row.insert("mimeType"_L1,     p.type.toLower() + "/"_L1 + p.subtype.toLower());
                row.insert("encodedBytes"_L1, p.bytes);
                row.insert("encoding"_L1,     p.encoding);
                attachments.push_back(row);
            }
            h.insert("attachments"_L1, attachments);
        }
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
        std::ranges::transform(requested, std::back_inserter(ids),
                               [](qint32 u) { return QString::number(u); });
        uidSpec = ids.join(',');
    }

    const QString gmailItems = ctx.isGmail() ? "X-GM-LABELS X-GM-MSGID X-GM-THRID "_L1 : QString{};
    const QString fetchCmd = QStringLiteral(
        "UID FETCH %1 (UID FLAGS INTERNALDATE %3BODYSTRUCTURE "
        "BODY.PEEK[HEADER.FIELDS (FROM TO CC SENDER REPLY-TO RETURN-PATH SUBJECT DATE MESSAGE-ID "
        "AUTHENTICATION-RESULTS X-MAILER IN-REPLY-TO REFERENCES RECEIVED "
        "X-MAILGUN-SID X-SG-EID X-SMTPAPI X-MC-USER X-KLAVIYO-MESSAGE-ID X-PM-MESSAGE-ID X-SES-OUTGOING "
        "LIST-ID LIST-UNSUBSCRIBE BIMI-LOCATION LIST-PREVIEW X-PREHEADER X-MC-PREVIEW-TEXT X-ALT-DESCRIPTION)] "
        "BODY.PEEK[]<0.%2>)"
    ).arg(uidSpec).arg(kInitialBodyPeekBytes).arg(gmailItems);


    const QString batchResp = ctx.cxn->execute(fetchCmd);

    if (!batchResp.contains(" OK"_L1, Qt::CaseInsensitive)) {
        return;
    }
    if (batchResp.contains("THROTTLED"_L1, Qt::CaseInsensitive)) {
        throttleBackoffMs = (throttleBackoffMs <= 0) ? 250 : qMin(2000, throttleBackoffMs * 2);
    } else if (throttleBackoffMs > 0) {
        throttleBackoffMs = qMax(0, throttleBackoffMs - 100);
    }

    const QStringList fetchParts    = splitFetchResponses(batchResp);

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
                    std::ranges::transform(sortedHints, std::back_inserter(quoted),
                                           [](const QString &hf) { return "\"%1\""_L1.arg(hf); });
                    fetchResp += "\nX-GM-LABELS (%1)\n"_L1.arg(quoted.join(' '));
                }
            }
        }

        ++state.fetchedCount;
        state.visibleUids.push_back(uid);
        if (extractGmailCategoryFolder(fetchResp).isEmpty()) ++state.categoryMissCount;

        ingestMessage(fetchResp, uid, debugPrefix, ctx.cxn->selectedFolder(),
                      ctx, state);
    }
}

// ── Cross-folder dedup pre-pass ───────────────────────────────────────────────

struct PrepassResult {
    std::vector<qint32> needFullFetch;
    int edgesInserted = 0;
};

PrepassResult
prepassMessageIds(SyncContext &ctx, const std::vector<qint32> &uids, const IngestState &state)
{
    PrepassResult result;
    if (uids.empty() || !ctx.lookupByMessageIdHeaders || !ctx.insertFolderEdge) {
        result.needFullFetch = uids;
        return result;
    }

    using SyncUtils::kSyncBatchSize;

    static const QRegularExpression kFlagsRe(
        R"(FLAGS\s*\(([^)]*)\))"_L1, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kSeenRe(
        R"(\\Seen)"_L1, QRegularExpression::CaseInsensitiveOption);

    // uid → {rawMessageIdHeader, unread}
    QHash<qint32, QPair<QString, int>> uidInfo;
    uidInfo.reserve(static_cast<int>(uids.size()));

    // Cheap batch fetch: just Message-ID header + FLAGS.
    for (int base = 0; base < static_cast<int>(uids.size()); base += kSyncBatchSize) {
        if (ctx.cancelRequested && ctx.cancelRequested->load()) break;

        const int end = std::min(base + kSyncBatchSize, static_cast<int>(uids.size()));
        std::vector<qint32> chunk(uids.begin() + base, uids.begin() + end);
        std::ranges::sort(chunk);

        QStringList uidStrs;
        uidStrs.reserve(static_cast<int>(chunk.size()));
        std::ranges::transform(chunk, std::back_inserter(uidStrs),
                               [](qint32 u) { return QString::number(u); });

        const QString resp = ctx.cxn->execute(
            "UID FETCH %1 (UID FLAGS BODY.PEEK[HEADER.FIELDS (MESSAGE-ID)])"_L1
                .arg(uidStrs.join(u',')));

        if (!resp.contains(" OK"_L1, Qt::CaseInsensitive)) continue;

        for (const QString &part : splitFetchResponses(resp)) {
            const QString uid = parseUidFromFetch(part);
            if (uid.isEmpty()) continue;
            bool ok = false;
            const qint32 uidV = uid.toInt(&ok);
            if (!ok) continue;

            const auto flagsM = kFlagsRe.match(part);
            const int unread = (flagsM.hasMatch() && kSeenRe.match(flagsM.captured(1)).hasMatch()) ? 0 : 1;

            const QString headerBlock = extractHeaderFieldsLiteral(part.toUtf8());
            const QString &src = headerBlock.isEmpty() ? part : headerBlock;
            const QString msgId = extractField(src, "Message-ID"_L1).trimmed();

            uidInfo.insert(uidV, {msgId, unread});
        }
    }

    // Account for any UIDs the server didn't respond for (pass them through).
    QSet<qint32> respondedUids;
    respondedUids.reserve(uidInfo.size());
    for (auto it = uidInfo.cbegin(); it != uidInfo.cend(); ++it)
        respondedUids.insert(it.key());
    std::ranges::copy_if(uids, std::back_inserter(result.needFullFetch),
                         [&](qint32 u) { return !respondedUids.contains(u); });

    // Collect non-empty Message-IDs for batch DB lookup.
    QStringList msgIdList;
    msgIdList.reserve(uidInfo.size());
    for (auto it = uidInfo.cbegin(); it != uidInfo.cend(); ++it) {
        if (!it.value().first.isEmpty())
            msgIdList.push_back(it.value().first);
    }

    const QMap<QString, qint64> known = msgIdList.isEmpty()
        ? QMap<QString, qint64>{}
        : ctx.lookupByMessageIdHeaders(ctx.cxn->email(), msgIdList);

    const QString &folder = ctx.cxn->selectedFolder();

    for (auto it = uidInfo.cbegin(); it != uidInfo.cend(); ++it) {
        const qint32 uid        = it.key();
        const QString &msgId    = it.value().first;
        const int unread        = it.value().second;

        if (!msgId.isEmpty() && known.contains(msgId)) {
            const qint64 existingRow = known.value(msgId);
            ctx.insertFolderEdge(ctx.cxn->email(), existingRow, folder, QString::number(uid), unread);

            // For Gmail INBOX: also insert the category folder edge so the message
            // appears in the right tab (Primary/Promotions/Social/etc.).
            if (ctx.isGmailInbox()) {
                const auto categoryFolders = state.categoryHints.value(uid);
                for (const QString &catFolder : categoryFolders) {
                    if (!catFolder.isEmpty())
                        ctx.insertFolderEdge(ctx.cxn->email(), existingRow,
                                             catFolder, QString::number(uid), unread);
                }
            }

            ++result.edgesInserted;
            qInfo().noquote() << "[dedup]"
                              << "folder=" << folder
                              << "uid=" << uid
                              << "msg-id=" << msgId
                              << "-> existing row" << existingRow
                              << "(skipped full fetch)";
        } else {
            result.needFullFetch.push_back(uid);
        }
    }

    return result;
}

// ── Incremental sync ──────────────────────────────────────────────────────────

SyncResult
executeIncremental(SyncContext &ctx) {
    SyncResult result;
    IngestState state;
    KestrelTimer folderSearchTimer;

    // Search for new UIDs above minUidExclusive.
    // CONDSTORE: if modseq is unchanged no new messages can exist, skip the search entirely.
    QStringList newUids;
    if (!ctx.condstoreUnchanged) {
        const auto sCmd = QStringLiteral("UID SEARCH UID %1:*").arg(ctx.minUidExclusive + 1);
        const auto sResp = ctx.cxn->execute(sCmd);
        newUids = parseSearchIds(sResp);

        // For Gmail INBOX: build per-category hints via X-GM-RAW searches as a reliable
        // fallback for when X-GM-LABELS in the FETCH response doesn't include the category
        // (common race condition with newly delivered messages).
        if (!newUids.isEmpty())
            buildCategoryHints(ctx, state, ctx.minUidExclusive + 1, INT_MAX);
    }

    // Filter to UIDs strictly greater than minUidExclusive, then sort and convert
    // to qint32 in one pipeline.
    std::vector<qint32> incUids;
    incUids.reserve(newUids.size());
    for (const auto &u : newUids) {
        bool ok = false;
        const qint32 v = u.toInt(&ok);
        if (ok && v > ctx.minUidExclusive)
            incUids.push_back(v);
    }
    std::ranges::sort(incUids);

    // Cross-folder dedup pre-pass for new UIDs.
    qint32 throttleBackoffMs = 0;
    using SyncUtils::kSyncBatchSize;

    if (!incUids.empty()) {
        const auto prepass = prepassMessageIds(ctx, incUids, state);
        if (prepass.edgesInserted > 0) {
            qInfo().noquote() << "[dedup] inc-sync folder=" << ctx.folderName
                              << "pre-pass resolved" << prepass.edgesInserted
                              << "of" << incUids.size() << "new UIDs via cross-folder dedup";
        }
        incUids = prepass.needFullFetch;
    }

    // Fetch remaining new UIDs (not resolved by dedup) in batches.
    std::array<qint32, kSyncBatchSize> incBatch{};
    int incN = 0;

    for (const qint32 v : incUids) {
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

    // Backfill messages within budget that aren't cached locally.
    if (ctx.fetchBudget != 0 && ctx.getFolderUids) {
        const auto localUids = ctx.getFolderUids(ctx.cxn->email(), ctx.folderName);
        const QSet localSet(localUids.begin(), localUids.end());
        QSet<QString> newSet;
        newSet.reserve(static_cast<qsizetype>(incUids.size()));
        for (const qint32 v : incUids)
            newSet.insert(QString::number(v));

        bool skipSearchAll = false;
        if (ctx.remoteExists >= 0) {
            Q_ASSERT_X(static_cast<bool>(ctx.getFolderMessageCount),
                       "SyncEngine::executeIncremental",
                       "getFolderMessageCount callback must be set");
            const qint64 localCount = ctx.getFolderMessageCount(ctx.cxn->email(), ctx.folderName);
            if (localCount == ctx.remoteExists)
                skipSearchAll = true;
        }
        if (skipSearchAll) {
            result.success                     = true;
            result.headers                     = state.out;
            result.fetchedCount                = state.fetchedCount;
            result.categoryMissCount           = state.categoryMissCount;
            result.missingAddressHeadersCount  = state.missingAddressHeadersCount;
            result.statusMessage               = QStringLiteral("Fetched %1 headers.").arg(state.out.size());
            return result;
        }

        QStringList allUids;
        if (ctx.hasSearchAllSnapshot) {
            allUids = ctx.searchAllUids;
        } else {
            const auto allResp = ctx.cxn->execute("UID SEARCH ALL"_L1);
            allUids = parseUidSearchAll(allResp);
            ctx.hasSearchAllSnapshot = true;
            ctx.searchAllUids = allUids;
        }

        std::ranges::sort(allUids, [](const QString &a, const QString &b) {
            return a.toLongLong() < b.toLongLong();
        });

        // Collect all backfill UIDs (budget-capped, newest first), run dedup pre-pass,
        // then full-fetch only the ones not already in the DB.
        std::vector<qint32> backfillCandidates;
        const int backfillBudget = (ctx.fetchBudget < 0) ? INT_MAX : ctx.fetchBudget;
        backfillCandidates.reserve(static_cast<int>(allUids.size()));
        for (auto i = allUids.size() - 1;
             i >= 0 && static_cast<int>(backfillCandidates.size()) < backfillBudget; --i) {
            const auto uid = allUids.at(i);
            if (newSet.contains(uid) || localSet.contains(uid)) continue;
            bool ok = false;
            const qint32 uidV = uid.toInt(&ok);
            if (ok) backfillCandidates.push_back(uidV);
        }

        if (!backfillCandidates.empty()) {
            // Build Gmail category hints for backfill UIDs so they get the correct
            // category folder edge (Social, Promotions, etc.) instead of bare INBOX.
            if (!backfillCandidates.empty()) {
                const auto [minBf, maxBf] = std::ranges::minmax(backfillCandidates);
                buildCategoryHints(ctx, state, minBf, maxBf);
            }

            const auto prepass = prepassMessageIds(ctx, backfillCandidates, state);
            if (prepass.edgesInserted > 0) {
                qInfo().noquote() << "[dedup] backfill folder=" << ctx.folderName
                                  << "pre-pass resolved" << prepass.edgesInserted
                                  << "of" << backfillCandidates.size()
                                  << "UIDs via cross-folder dedup";
            }

            int backfillN = 0;
            std::array<qint32, kSyncBatchSize> backfillBatch{};
            auto flushBackfill = [&]() {
                if (backfillN <= 0) return;
                if (throttleBackoffMs > 0)
                    QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));
                fetchUidBatch(ctx,
                              std::vector<qint32>(backfillBatch.begin(), backfillBatch.begin() + backfillN),
                              "folder-backfill"_L1, state, throttleBackoffMs);
                backfillN = 0;
            };

            for (const qint32 uidV : prepass.needFullFetch) {
                backfillBatch[backfillN++] = uidV;
                if (backfillN == kSyncBatchSize)
                    flushBackfill();
            }
            flushBackfill();
        }
    }

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
    }

    QSet<qint32> pendingUidSet;
    int fetchedTotal      = 0;
    int throttleBackoffMs = 0;

    QStringList allIds;
    bool skipSearchAll = false;
    if (ctx.remoteExists >= 0) {
        Q_ASSERT_X(static_cast<bool>(ctx.getFolderMessageCount),
                   "SyncEngine::executeFull",
                   "getFolderMessageCount callback must be set");
        const qint64 localCount = ctx.getFolderMessageCount(ctx.cxn->email(), ctx.folderName);
        if (localCount == ctx.remoteExists)
            skipSearchAll = true;
    }

    // UID SEARCH ALL to collect all remote UIDs (unless local count already matches EXISTS).
    if (!skipSearchAll) {
        const QString allResp = ctx.cxn->execute("UID SEARCH ALL"_L1);
        allIds = parseSearchIds(allResp);
    }

    searchAllTimer.restart();
    for (const QString &id : allIds) {
        bool ok = false;
        const qint32 v = id.toInt(&ok);
        if (ok && v > 0 && !knownFolderUids.contains(v)) pendingUidSet.insert(v);
    }

    // Build per-category hints via X-GM-RAW searches before fetching messages.
    // X-GM-LABELS in the FETCH response is unreliable for bulk/initial fetches —
    // the X-GM-RAW "category:..." search is authoritative and fills the fallback
    // hint map used in fetchUidBatch when extractGmailCategoryFolder returns empty.
    if (!pendingUidSet.isEmpty())
        buildCategoryHints(ctx, state, 1, INT_MAX);

    // Cross-folder dedup pre-pass: cheaply fetch Message-ID + FLAGS for all pending
    // UIDs and insert folder edges for messages already in the DB from another folder.
    // Only the UIDs that are truly new need a full FETCH.
    if (!pendingUidSet.isEmpty()) {
        std::vector<qint32> pendingVec(pendingUidSet.cbegin(), pendingUidSet.cend());
        const auto prepass = prepassMessageIds(ctx, pendingVec, state);
        if (prepass.edgesInserted > 0) {
            qInfo().noquote() << "[dedup] full-sync folder=" << ctx.folderName
                              << "pre-pass resolved" << prepass.edgesInserted
                              << "of" << pendingVec.size() << "UIDs via cross-folder dedup";
        }
        pendingUidSet.clear();
        for (const qint32 u : prepass.needFullFetch)
            pendingUidSet.insert(u);
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

        std::vector<qint32> pending(pendingUidSet.cbegin(), pendingUidSet.cend());
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


            if (throttleBackoffMs > 0)
                QThread::msleep(static_cast<unsigned long>(throttleBackoffMs));
            fetchUidBatch(ctx, chunk, "inbox"_L1, state, throttleBackoffMs);
        }
        fetchedTotal += static_cast<int>(pending.size());
        pendingUidSet.clear();
    };

    flushPending();

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
// SyncEngine public API
// ─────────────────────────────────────────────────────────────────────────────

QVariantList
SyncEngine::fetchFolders(std::shared_ptr<Connection> cxn, QString *statusOut, const bool refresh) {
    if (!cxn) {
        if (statusOut) *statusOut = "No connection for folder fetch."_L1;
        return {};
    }

    const QString email = cxn->email();

    static QVariantList cachedFolders;
    if (!refresh && !cachedFolders.isEmpty()) {
        if (statusOut) *statusOut = QStringLiteral("Using cached folders (%1).").arg(cachedFolders.size());
        return cachedFolders;
    }

    QVariantList out = cxn->list();
    for (auto &entry : out) {
        auto row = entry.toMap();
        row.insert("accountEmail"_L1, email);
        entry = row;
    }

    if (out.isEmpty()) {
        if (statusOut) *statusOut = "Using default Gmail folders."_L1;
        cachedFolders = defaultGmailFolders(email);
        return cachedFolders;
    }

    cachedFolders = out;
    if (statusOut) *statusOut = QStringLiteral("Fetched %1 folders.").arg(out.size());
    return out;
}

SyncResult
SyncEngine::execute(SyncContext &ctx) {
    KestrelTimer flagsReconcileTimer;

    if (!ctx.cxn) {
        SyncResult r;
        r.success = false;
        r.statusMessage = "No connection provided"_L1;
        return r;
    }

    // ── Step 1: Select/examine the folder and capture HIGHESTMODSEQ ──────────
    //
    // If the connection already has this folder selected, issue STATUS instead
    // of a redundant EXAMINE.  Both responses include HIGHESTMODSEQ (when
    // CONDSTORE is enabled) and MESSAGES, so modseq tracking works in either path.
    qint64 remoteMessages = -1;
    qint64 examineModSeq  = 0;   // HIGHESTMODSEQ from EXAMINE or STATUS response

    const bool alreadySelected = (ctx.cxn->selectedFolder().compare(ctx.folderName, Qt::CaseInsensitive) == 0);
    if (alreadySelected) {
        const QString statusResp = ctx.cxn->execute(
            "STATUS \"%1\" (UIDNEXT HIGHESTMODSEQ MESSAGES)"_L1.arg(ctx.folderName));
        remoteMessages = parseMessagesFromStatusResponse(statusResp);
        examineModSeq  = parseHighestModSeq(statusResp);
    }

    if (remoteMessages < 0) {
        // EXAMINE (read-only) the target folder — sync never writes flags, so READ-WRITE
        // access is unnecessary and would incorrectly clear \Recent flags.
        // Falls back through [Gmail]/[Google Mail] alias if the first attempt fails.
        auto selResult = ctx.cxn->examine(ctx.folderName);
        if (!selResult) {
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

            bool selectedAlias = false;
            for (const QString &alt : aliases) {
                selResult = ctx.cxn->examine(alt);
                if (selResult) {
                    selectedAlias = true;
                    break;
                }
            }
            if (!selectedAlias) {
                SyncResult r;
                r.success = false;
                r.statusMessage = "SELECT failed for folder: %1"_L1.arg(ctx.folderName);
                return r;
            }
        }

        remoteMessages = parseExistsFromSelectResponse(*selResult);
        examineModSeq  = parseHighestModSeq(*selResult);
    }

    // ── Step 2: CONDSTORE — compare server modseq with our last-sync modseq ──
    //
    // If HIGHESTMODSEQ is unchanged since our last successful sync, the server
    // has seen no additions, deletions, or flag changes in this folder.
    // We can safely skip UID SEARCH ALL (deletion reconcile) and the new-message
    // search in executeIncremental.  The result is surfaced via ctx.condstoreUnchanged
    // so sub-functions can consult it without needing an extra parameter.
    ctx.condstoreUnchanged = (examineModSeq > 0
                               && ctx.lastHighestModSeq > 0
                               && examineModSeq == ctx.lastHighestModSeq);
    if (ctx.condstoreUnchanged) {
        qInfo().noquote() << "[condstore] skip" << ctx.folderName
                          << "modseq=" << examineModSeq << "(unchanged since last sync)";
    }

    ctx.remoteExists = remoteMessages;
    if (remoteMessages == 0) {
        // Zero-message folder: skip SEARCH-based operations entirely.
        if (ctx.reconcileDeletes && ctx.pruneFolder) {
            ctx.pruneFolder(ctx.cxn->email(), ctx.folderName, {});
        }
        SyncResult r;
        r.success = true;
        r.statusMessage = "Fetched 0 headers."_L1;
        if (examineModSeq > 0 && ctx.onSyncStateUpdated)
            ctx.onSyncStateUpdated(examineModSeq);
        return r;
    }

    // ── Step 3: Delete reconciliation ────────────────────────────────────────
    //
    // Compare remote UID set to local and purge deleted UIDs.
    // Runs only when explicitly requested (non-announce incremental folder syncs).
    // CONDSTORE: skip UID SEARCH ALL entirely when modseq says nothing changed —
    // an unchanged modseq guarantees no messages were expunged.
    if (ctx.reconcileDeletes && ctx.pruneFolder) {
        bool skipSearchAll = ctx.condstoreUnchanged;  // CONDSTORE skip
        if (!skipSearchAll && ctx.remoteExists >= 0) {
            Q_ASSERT_X(static_cast<bool>(ctx.getFolderMessageCount),
                       "SyncEngine::execute(reconcileDeletes)",
                       "getFolderMessageCount callback must be set");
            const qint64 localCount = ctx.getFolderMessageCount(ctx.cxn->email(), ctx.folderName);
            if (localCount == ctx.remoteExists)
                skipSearchAll = true;
        }

        if (!skipSearchAll) {
            const QString allResp = ctx.cxn->execute("UID SEARCH ALL"_L1);
            const QStringList remoteUids = parseUidSearchAll(allResp);
            ctx.hasSearchAllSnapshot = true;
            ctx.searchAllUids = remoteUids;
            // Folder-scoped prune: only removes edges from THIS folder (not cross-folder),
            // preventing e.g. a Trash prune from wiping All Mail edges for messages that
            // are still present in All Mail on the server.
            ctx.pruneFolder(ctx.cxn->email(), ctx.folderName, remoteUids);
        }
    }

    // ── Step 4: FLAGS reconciliation ─────────────────────────────────────────
    //
    // For incremental syncs, search for SEEN UIDs in a recent window to pick up
    // \Seen changes from other clients (e.g. phone or webmail).
    // We only mark messages read — never unread — so this never overrides local state.
    // CONDSTORE: skip when modseq is unchanged (no flag changes can have occurred).
    if (!ctx.condstoreUnchanged && ctx.minUidExclusive > 0 && ctx.onFlagsReconciled) {
        bool envOk = false;
        const int configuredWindow = qEnvironmentVariableIntValue("KESTREL_FLAG_RECON_WINDOW", &envOk);
        const qint64 reconWindow = envOk && configuredWindow > 0 ? configuredWindow : 2000;
        const qint64 windowStart = qMax(qint64(1), ctx.minUidExclusive - reconWindow + 1);

        const QString flagResp = ctx.cxn->execute(
            QStringLiteral("UID SEARCH UID %1:%2 SEEN")
                .arg(windowStart).arg(ctx.minUidExclusive));
        const QStringList seenUids = parseSearchIds(flagResp);
        if (!seenUids.isEmpty()) {
            ctx.onFlagsReconciled(ctx.cxn->email(), ctx.cxn->selectedFolder(), seenUids);
        }

        if (ctx.onFlaggedReconciled) {
            const QString flaggedResp = ctx.cxn->execute(
                QStringLiteral("UID SEARCH UID %1:%2 FLAGGED")
                    .arg(windowStart).arg(ctx.minUidExclusive));
            const QStringList flaggedUids = parseSearchIds(flaggedResp);
            if (!flaggedUids.isEmpty()) {
                ctx.onFlaggedReconciled(ctx.cxn->email(), ctx.cxn->selectedFolder(), flaggedUids);
            }
        }
    }

    // ── Step 5: Fetch new/missing messages ───────────────────────────────────
    SyncResult result;
    if (ctx.minUidExclusive > 0)
        result = executeIncremental(ctx);
    else
        result = executeFull(ctx);

    // Persist the modseq we synced against so the next sync can use CONDSTORE.
    if (result.success && examineModSeq > 0 && ctx.onSyncStateUpdated)
        ctx.onSyncStateUpdated(examineModSeq);

    return result;
}

} // namespace Imap
