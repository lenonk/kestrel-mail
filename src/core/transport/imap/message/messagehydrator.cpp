#include "messagehydrator.h"
#include "bodyprocessor.h"

#include <QDebug>
#include <QElapsedTimer>

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
        const auto [selectOk, _] = req.cxn->select(folder);
        const qint64 selectMs = step.elapsed();

        if (!selectOk) {
            qWarning().noquote() << "[perf-hydrate-exec]"
                                 << "uid=" << uid
                                 << "folder=" << folder
                                 << "selectMs=" << selectMs
                                 << "result=select-failed";
            continue;
        }

        step.restart();
        const auto raw = req.cxn->executeRaw("UID FETCH %1 (BODY.PEEK[])"_L1.arg(uid));
        const qint64 fetchMs = step.elapsed();

        const auto hasFetchData = raw.contains(" FETCH (") || raw.contains(" fetch (");
        const bool hasLiteral = raw.contains('{');

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
        const QString html = extractBodyHtmlFromFetch(raw).trimmed();
        const qint64 parseMs = step.elapsed();

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
