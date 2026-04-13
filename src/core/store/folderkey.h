#pragma once

#include <QString>

namespace FolderKey {

struct AccountFolder {
    QString accountEmail;   // empty for aggregate/favorites/tags
    QString folder;         // normalized folder name
};

// Parse "account:lenon@gmail.com:inbox" → {accountEmail, folder}
// Legacy "account:inbox" → {empty, "inbox"}
// "favorites:all-inboxes" → {empty, empty}
inline AccountFolder
parseAccountKey(const QString &key) {
    static constexpr auto kPrefix = QLatin1StringView("account:");
    if (!key.startsWith(kPrefix, Qt::CaseInsensitive))
        return {};

    const auto rest = key.mid(kPrefix.size());
    const auto colonIdx = rest.indexOf(u':');

    // New format: "email:folder" — email always has '@'
    if (colonIdx > 0 && rest.left(colonIdx).contains(u'@')) {
        return {
            rest.left(colonIdx).toLower().trimmed(),
            rest.mid(colonIdx + 1).toLower().trimmed()
        };
    }

    // Legacy format: just "folder" (no email)
    return { {}, rest.toLower().trimmed() };
}

// Build "account:lenon@gmail.com:inbox"
inline QString
buildAccountKey(const QString &accountEmail, const QString &folder) {
    if (accountEmail.isEmpty())
        return QLatin1StringView("account:") + folder.toLower();
    return QLatin1StringView("account:") + accountEmail.toLower() + QLatin1Char(':') + folder.toLower();
}

} // namespace FolderKey
