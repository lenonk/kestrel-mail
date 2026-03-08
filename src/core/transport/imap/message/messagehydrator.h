#pragma once

#include <QString>
#include <QVector>
#include <QPair>

namespace Imap {

/**
 * Fetches the full HTML body for a single message.
 */
class MessageHydrator {
public:
    struct Request {
        QString host;
        int     port        = 0;
        QString email;
        QString accessToken;
        QString folderName;
        QString uid;

        // Extra folder+uid pairs from the local DB (fetched by the caller before
        // dispatching to a worker thread).  The primary folderName+uid is always
        // tried first; these are attempted in order if the primary fails.
        QVector<QPair<QString, QString>> extraCandidates;
    };

    /**
     * Connect to the IMAP server, authenticate, and fetch the full RFC-822
     * body for the requested message.
     *
     * @return HTML body string, or empty string on failure.
     */
    [[nodiscard]] static QString execute(const Request &req);
};

} // namespace Imap
