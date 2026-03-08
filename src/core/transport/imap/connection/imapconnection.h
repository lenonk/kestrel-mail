#pragma once

#include <QSslSocket>
#include <QVariantList>

#include <memory>
#include <tuple>

namespace Imap {

using namespace Qt::Literals::StringLiterals;

/**
 * Result of a connection attempt.
 */
struct ConnectResult {
    bool success = false;
    QString message;
    QString capabilities;
};

/**
 * Manages IMAP socket lifecycle, TLS, and authentication.
 * Encapsulates connection state and provides clean API for sync operations.
 * The socket is fully private; callers interact only through the public API.
 */
class Connection {
public:
    Connection();
    ~Connection();

    /**
     * Connect to IMAP server with TLS and authenticate via XOAUTH2.
     * Saves host/email/accessToken internally for later retrieval.
     *
     * @param host IMAP server hostname
     * @param port IMAP server port (typically 993)
     * @param email User email for authentication
     * @param accessToken OAuth2 access token
     * @return ConnectResult with success status and details
     */
    ConnectResult connectAndAuth(const QString &host, int port,
                                  const QString &email, const QString &accessToken);

    // Gracefully disconnect from server (sends LOGOUT).
    void disconnect();

    // Check if connection is active.
    [[nodiscard]] bool isConnected() const;

    // Get server capabilities (populated after connectAndAuth).
    [[nodiscard]] QString capabilities()     const { return m_capabilities; }

    // Connection parameters saved during connectAndAuth.
    [[nodiscard]] QString host()             const { return m_host; }
    [[nodiscard]] QString email()            const { return m_email; }
    [[nodiscard]] QString accessToken()      const { return m_accessToken; }
    [[nodiscard]] QString selectedFolder()   const { return m_selectedFolder; }

    // Execute a generic IMAP command (tag managed internally). Returns response as QString.
    [[nodiscard]] QString execute(const QString &command);

    // Like execute(), but returns the raw QByteArray response — use for FETCH commands
    // where the literal payload may contain non-UTF-8 binary data.
    [[nodiscard]] QByteArray executeRaw(const QString &command);

    // List server folders. Uses XLIST for Gmail, LIST otherwise.
    // Returns QVariantList of rows: { name, flags, specialUse }.
    [[nodiscard]] QVariantList list();

    // Send SELECT for mailbox; updates selectedFolder() on success.
    // Returns {success, raw server response}.
    [[nodiscard]] std::tuple<bool, QString> select(const QString &mailbox);

    // Enter IMAP IDLE mode for the currently selected mailbox.
    // Returns server continuation line (typically starts with '+').
    // On failure, returns an error string prefixed with "IDLE failed:".
    [[nodiscard]] std::tuple<bool, QString> enterIdle();

    // Wait for untagged push bytes while in IDLE. Returns empty string on timeout/no data.
    [[nodiscard]] QString waitForIdlePush(int timeoutMs) const;

    // Exit the active IDLE command by sending DONE and waiting for tagged completion.
    [[nodiscard]] std::tuple<bool, QString> exitIdle();


    [[nodiscard]] bool isGmail() const {
        return !m_host.isEmpty() && m_host.contains("gmail"_L1, Qt::CaseInsensitive);
    }

private:
    std::unique_ptr<QSslSocket> m_socket;

    qint32  m_tag          = 1;
    QString m_capabilities;
    bool    m_authenticated = false;

    QString m_host;
    QString m_email;
    QString m_accessToken;
    QString m_selectedFolder;
    QString m_idleTag;

    QString nextTag();
};

} // namespace Imap
