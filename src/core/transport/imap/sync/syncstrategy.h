#pragma once

#include "synccontext.h"

namespace Imap {

/**
 * Base interface for IMAP folder sync strategies.
 * Different implementations handle incremental vs full sync,
 * Gmail-specific category logic, etc.
 */
class SyncStrategy {
public:
    virtual ~SyncStrategy() = default;
    
    /**
     * Execute sync operation with given context.
     * @param ctx Sync configuration and callbacks
     * @return SyncResult with headers and status
     */
    virtual SyncResult execute(SyncContext &ctx) = 0;
    
protected:
    /**
     * Helper: Parse UID set from comma/range spec into sorted vector.
     * Example: "1,5,10:15" → [1,5,10,11,12,13,14,15]
     */
    static std::vector<qint32> parseUidSet(const QStringList &uids);
    
    /**
     * Helper: Build UID range/set spec for FETCH from sorted UIDs.
     * Uses ranges for contiguous UIDs, comma-separated otherwise.
     * Example: [1,2,3,5,7,8,9] → "1:3,5,7:9"
     */
    static QString buildUidSpec(const std::vector<qint32> &uids);
};

} // namespace Imap
