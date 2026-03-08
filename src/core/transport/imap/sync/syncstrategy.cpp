#include "syncstrategy.h"
#include <algorithm>

namespace Imap {

std::vector<qint32>
SyncStrategy::parseUidSet(const QStringList &uids) {
    std::vector<qint32> result;
    result.reserve(uids.size());
    
    for (const QString& uid : uids) {
        auto ok = false;
        if (const qint32 v = uid.toInt(&ok); ok && v > 0) {
            result.push_back(v);
        }
    }
    
    std::ranges::sort(result);
    return result;
}

QString
SyncStrategy::buildUidSpec(const std::vector<qint32> &uids) {
    if (uids.empty())
        return {};

    if (uids.size() == 1)
        return QString::number(uids[0]);
    
    // Build ranges for contiguous sequences
    QStringList parts;
    size_t i = 0;
    while (i < uids.size()) {
        const int32_t start = uids[i];
        auto j = i + 1;
        
        // Find end of contiguous sequence
        while (j < uids.size() && uids[j] == uids[j - 1] + 1) {
            ++j;
        }
        
        const auto end = uids[j - 1];
        
        if (j - i > 2) {
            // Use range notation for 3+ contiguous UIDs
            parts.append(QStringLiteral("%1:%2").arg(start).arg(end));
        }
        else {
            // Comma-separated for short sequences
            for (auto k = i; k < j; ++k) {
                parts.append(QString::number(uids[k]));
            }
        }
        
        i = j;
    }
    
    return parts.join(',');
}

} // namespace Imap
