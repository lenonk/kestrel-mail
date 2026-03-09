#include "messagehydrator.h"
#include "bodyprocessor.h"
#include "../connection/imapconnection.h"

#include <QDebug>
#include <memory>

using namespace Qt::Literals::StringLiterals;
using Imap::BodyProcessor::extractBodyHtmlFromFetch;

namespace Imap {

QString MessageHydrator::execute(const Request &req) {
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

    // ── Connect and authenticate ──────────────────────────────────────────────
    auto cxn = std::make_unique<Connection>();
    if (const auto cr = cxn->connectAndAuth(req.host, req.port, req.email, req.accessToken); !cr.success) {
        return {};
    }

    // ── Try each candidate in order ───────────────────────────────────────────
    for (const auto &[folder, uid] : candidates) {
        const auto [selectOk, _] = cxn->select(folder);

        if (!selectOk)
            continue;

        const auto raw = cxn->executeRaw("UID FETCH %1 (BODY.PEEK[])"_L1.arg(uid));
        const auto hasFetchData = raw.contains(" FETCH (") || raw.contains(" fetch (");

        if (const bool hasLiteral = raw.contains('{'); !hasFetchData || !hasLiteral) {
            continue;
        }

        const QString html = extractBodyHtmlFromFetch(raw).trimmed();
        if (!html.isEmpty())
            return html;
    }

    qWarning().noquote() << "[hydrate] no-html-extracted" << "uid=" << req.uid;
    return {};
}

} // namespace Imap
