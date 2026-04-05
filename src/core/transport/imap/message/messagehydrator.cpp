#include "messagehydrator.h"

#include "bodyprocessor.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;
using Imap::BodyProcessor::extractBodyHtmlFromFetch;

namespace Imap {

QString MessageHydrator::execute(const Request &req) {
    if (!req.cxn)
        return {};

    // Build the ordered candidate list: primary folder+uid first, then extras.
    struct Candidate { QString folder; QString uid; };
    QVector<Candidate> candidates;
    candidates.push_back( { req.folderName.trimmed(), req.uid.trimmed() } );

    QSet<QString> seen;
    seen.insert(req.folderName.trimmed().toLower() + "|" + req.uid.trimmed());
    for (const auto &[folder, uid] : req.extraCandidates) {
        const auto ft = folder.trimmed();
        const auto ut = uid.trimmed();

        if (ft.isEmpty() || ut.isEmpty())
            continue;

        const auto key = ft.toLower() + "|" + ut;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        candidates.push_back({ft, ut});
    }

    // ── Try each candidate in order ───────────────────────────────────────────
    QElapsedTimer totalTimer;
    totalTimer.start();

    for (const auto &[folder, uid] : candidates) {
        QElapsedTimer step;
        step.start();
        bool selectOk = true;
        QString msg;
        if (req.cxn->selectedFolder().compare(folder, Qt::CaseInsensitive) != 0) {
            const auto sel = req.cxn->examine(folder);
            selectOk = sel.has_value();
            msg = selectOk ? *sel : sel.error();
        }
        const qint64 selectMs = step.elapsed();

        if (!selectOk) {
            // Empty response means the socket died (server idle-timeout, etc.).
            // Try reconnecting in-place with stored credentials and retry EXAMINE.
            if (msg.isEmpty() && req.cxn->tryReconnect()) {
                const auto sel2 = req.cxn->examine(folder);
                selectOk = sel2.has_value();
                msg      = selectOk ? *sel2 : sel2.error();
            }

            if (!selectOk) {
                qWarning().noquote() << "[perf-hydrate-exec]"
                                     << "uid=" << uid
                                     << "folder=" << folder
                                     << "selectMs=" << selectMs
                                     << "selectErr=" << msg.simplified().left(120);
                // If the connection is dead and couldn't be recovered, bail out.
                // Remaining candidates would all fail with BAD on the
                // unauthenticated socket.
                if (!req.cxn->isConnected())
                    break;
                continue;
            }
        }

        step.restart();
        // First try a bounded full-message window (includes headers + initial body)
        // so mail parser still has RFC822 header context.
        auto raw = req.cxn->executeRaw("UID FETCH %1 (BODY.PEEK[]<0.131072>)"_L1.arg(uid));
        qint64 fetchMs = step.elapsed();

        auto hasFetchData = raw.contains(" FETCH (") || raw.contains(" fetch (");
        auto hasLiteral = raw.contains('{');

        if (!hasFetchData || !hasLiteral) {
            // Fallback for servers/messages where BODY[TEXT] window misses usable literal.
            step.restart();
            raw = req.cxn->executeRaw("UID FETCH %1 (BODY.PEEK[])"_L1.arg(uid));
            fetchMs += step.elapsed();
            hasFetchData = raw.toLower().contains(" fetch (");
            hasLiteral = raw.contains('{');
        }

        if (!hasFetchData || !hasLiteral) {
            qWarning().noquote() << "[perf-hydrate-exec]"
                                 << "uid=" << uid
                                 << "folder=" << folder
                                 << "selectMs=" << selectMs
                                 << "fetchMs=" << fetchMs
                                 << "rawLen=" << raw.size()
                                 << "hasFetch=" << hasFetchData
                                 << "hasLiteral=" << hasLiteral
                                 << "result=fetch-miss";
            continue;
        }

        step.restart();
        QString html = extractBodyHtmlFromFetch(raw).trimmed();
        qint64 parseMs = step.elapsed();

        // If bounded window produced suspiciously tiny HTML, only a plain-text
        // fallback, or unresolved CID image refs, the 128 KB window likely missed
        // related MIME parts. Retry with the full body so mailio can inline CIDs.
        const bool isPlainTextFallback = html.contains("white-space:normal"_L1);
        const bool hasUnresolvedCidSrc = QRegularExpression("\\bsrc\\s*=\\s*[\"']\\s*cid:"_L1,
                                                            QRegularExpression::CaseInsensitiveOption)
                                             .match(html).hasMatch();
        // Also retry if the bounded fetch was truncated: server returned exactly 131072 bytes,
        // meaning the message is larger than the window and the HTML was cut mid-content.
        const bool wasBoundedTruncated = raw.contains(QByteArrayLiteral("{131072}\r\n"));

        if (!html.isEmpty() && (html.size() < 512 || isPlainTextFallback || hasUnresolvedCidSrc || wasBoundedTruncated)) {
            step.restart();
            const auto rawFull = req.cxn->executeRaw("UID FETCH %1 (BODY.PEEK[])"_L1.arg(uid));
            fetchMs += step.elapsed();
            step.restart();
            const auto htmlFull = extractBodyHtmlFromFetch(rawFull).trimmed();
            parseMs += step.elapsed();
            if (!htmlFull.isEmpty()) {
                raw = rawFull;
                html = htmlFull;
            }
        }

        qWarning().noquote() << "[perf-hydrate-exec]"
                             << "uid=" << uid
                             << "folder=" << folder
                             << "selectMs=" << selectMs
                             << "fetchMs=" << fetchMs
                             << "parseMs=" << parseMs
                             << "rawLen=" << raw.size()
                             << "htmlLen=" << html.size();

        if (!html.isEmpty())
            return html;
    }

    qWarning().noquote() << "[perf-hydrate-exec]"
                         << "uid=" << req.uid
                         << "candidates=" << candidates.size()
                         << "totalMs=" << totalTimer.elapsed()
                         << "result=no-html";
    return {};
}

} // namespace Imap
