#pragma once

#include "synccontext.h"
#include <QVariantList>

namespace Imap {

/**
 * Standard folder sync implementation.
 * Handles both incremental (UID-based) and full sync modes.
 * Supports Gmail category enrichment when appropriate.
 *
 * execute() requires ctx.cxn to be an authenticated Connection (post-connectAndAuth).
 * SELECT is performed internally by execute(), including [Gmail]/[Google Mail] alias
 * fallback. The caller does not need to SELECT before calling execute().
 */
class SyncEngine {
public:
    /**
     * Fetch the folder list for an account.
     * Opens its own connection internally; the caller does not need a socket.
     */
    static QVariantList fetchFolders(const QString &host,
                                     qint32 port,
                                     const QString &email,
                                     const QString &accessToken,
                                     QString *statusOut);

    /**
     * Execute folder sync using the connected socket in ctx.
     * Routes to incremental (ctx.minUidExclusive > 0) or full sync.
     */
    SyncResult execute(SyncContext &ctx);
};

} // namespace Imap
