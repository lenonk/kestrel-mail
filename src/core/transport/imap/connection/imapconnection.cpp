#include "imapconnection.h"
#include "imapio.h"

#include <QRegularExpression>

using namespace Qt::Literals::StringLiterals;

namespace Imap {
namespace {

QByteArray buildXOAuth2Command(const QString &tag, const QString &email, const QString &accessToken) {
    const QByteArray authRaw = QStringLiteral("user=%1\u0001auth=Bearer %2\u0001\u0001").arg(email, accessToken).toUtf8();
    return tag.toUtf8() + " AUTHENTICATE XOAUTH2 " + authRaw.toBase64() + "\r\n";
}

QByteArray buildSelectCommand(const QString &tag, const QString &mailbox) {
    const QString quotedMailbox = QStringLiteral("\"%1\"").arg(mailbox);
    return tag.toUtf8() + " SELECT " + quotedMailbox.toUtf8() + "\r\n";
}

QByteArray buildSimpleCommand(const QString &tag, const QString &command) {
    return tag.toUtf8() + " " + command.toUtf8() + "\r\n";
}

QVariantList parseListResponse(const QString &listResp) {
    QVariantList out;
    static const QRegularExpression eolRe("\r?\n");

    for (const auto lines = listResp.split(eolRe, Qt::SkipEmptyParts); const auto &rawLine : lines) {
        const auto line = rawLine.trimmed();
        if (!line.startsWith("* LIST "_L1, Qt::CaseInsensitive)
            && !line.startsWith("* XLIST "_L1, Qt::CaseInsensitive))
            continue;

        const auto flagsStart = line.indexOf('(');
        const auto flagsEnd   = line.indexOf(')', flagsStart + 1);
        if (flagsStart < 0 || flagsEnd <= flagsStart)
            continue;

        const auto flags = line.mid(flagsStart + 1, flagsEnd - flagsStart - 1).trimmed();

        const auto nameStart = line.lastIndexOf('"');
        QString name;
        if (nameStart > 0) {
            if (const auto nfq = line.lastIndexOf('"', nameStart - 1); nfq >= 0 && nameStart > nfq)
                name = line.mid(nfq + 1, nameStart - nfq - 1);
        }
        if (name.isEmpty()) {
            if (const auto tail = line.lastIndexOf(' '); tail > flagsEnd)
                name = line.mid(tail + 1).trimmed();
        }
        if (name.isEmpty())
            continue;

        if (name.startsWith('"') && name.endsWith('"') && name.size() >= 2)
            name = name.mid(1, name.size() - 2);
        name.replace("\\\""_L1, "\""_L1);

        QString specialUse;
        if      (flags.contains("\\Inbox"_L1,  Qt::CaseInsensitive)) specialUse = "inbox"_L1;
        else if (flags.contains("\\Sent"_L1,   Qt::CaseInsensitive)) specialUse = "sent"_L1;
        else if (flags.contains("\\Trash"_L1,  Qt::CaseInsensitive)) specialUse = "trash"_L1;
        else if (flags.contains("\\Drafts"_L1, Qt::CaseInsensitive)) specialUse = "drafts"_L1;
        else if (flags.contains("\\Junk"_L1,   Qt::CaseInsensitive)) specialUse = "junk"_L1;
        else if (flags.contains("\\All"_L1,    Qt::CaseInsensitive)) specialUse = "all"_L1;
        if (name.compare("INBOX"_L1, Qt::CaseInsensitive) == 0)
            specialUse = "inbox"_L1;

        QVariantMap row;
        row.insert("name"_L1, name);
        row.insert("flags"_L1, flags);
        row.insert("specialUse"_L1, specialUse);
        out.push_back(row);
    }

    return out;
}

} // namespace

Connection::Connection()
    : m_socket(std::make_unique<QSslSocket>()) { }

Connection::~Connection()
{
    disconnect();
}

ConnectResult
Connection::connectAndAuth(const QString &host, const qint32 port,
                           const QString &email, const QString &accessToken) {
    ConnectResult result;

    if (accessToken.isEmpty()) {
        result.message = "No access token available";
        return result;
    }

    // Reset state
    m_socket = std::make_unique<QSslSocket>();
    m_authenticated = false;
    m_capabilities.clear();
    m_tag = 1;
    m_host        = host;
    m_email       = email;
    m_accessToken = accessToken;
    m_idleTag.clear();

    // Connect with TLS
    m_socket->connectToHostEncrypted(host, static_cast<quint16>(port));
    if (!m_socket->waitForEncrypted(IO::kTlsTimeoutMs)) {
        result.message = "TLS connect failed: %1"_L1.arg(m_socket->errorString());
        return result;
    }

    // Wait for server greeting
    if (!m_socket->waitForReadyRead(IO::kReadTimeoutMs)) {
        result.message = "No server greeting received";
        return result;
    }
    (void)m_socket->readAll();

    // Authenticate with XOAUTH2
    const auto authTag = nextTag();
    m_socket->write(buildXOAuth2Command(authTag, email, accessToken));
    m_socket->flush();

    auto imapResp = IO::readUntilTagged(*m_socket, authTag, IO::kTaggedReadTimeoutMs);

    // Handle SASL continuation if present
    if (imapResp.contains("\r\n+ "_L1) || imapResp.startsWith("+ "_L1)) {
        m_socket->write("\r\n");
        m_socket->flush();
        imapResp += IO::readUntilTagged(*m_socket, authTag, IO::kTaggedReadTimeoutMs);
    }

    if (!imapResp.contains(authTag + " OK"_L1, Qt::CaseInsensitive)) {
        result.message = "Authentication failed: %1"_L1.arg(imapResp.simplified().left(200));
        return result;
    }

    m_authenticated = true;

    // Fetch capabilities
    const auto capTag = nextTag();
    m_socket->write(buildSimpleCommand(capTag, "CAPABILITY"));
    m_socket->flush();
    imapResp = IO::readUntilTagged(*m_socket, capTag, IO::kFetchReadTimeoutMs);
    if (!imapResp.contains(capTag + " OK"_L1, Qt::CaseInsensitive)) {
        result.message = "CAPABILITY failed: %1"_L1.arg(imapResp.simplified().left(200));
        return result;
    }
    m_capabilities = imapResp.simplified();

    // Enable UTF-8 acceptance (kept intentionally)
    const auto enableTag = nextTag();
    m_socket->write(buildSimpleCommand(enableTag, "ENABLE UTF8=ACCEPT"));
    m_socket->flush();
    imapResp = IO::readUntilTagged(*m_socket, enableTag, IO::kFetchReadTimeoutMs);
    if (!imapResp.contains(enableTag + " OK"_L1, Qt::CaseInsensitive)) {
        result.message = "ENABLE UTF8=ACCEPT failed: %1"_L1.arg(imapResp.simplified().left(200));
        return result;
    }

    // Mailspring-parity extras (best-effort, do not fail connection):
    // 1) ENABLE QRESYNC / CONDSTORE when advertised
    if (m_capabilities.contains("QRESYNC"_L1, Qt::CaseInsensitive)
        || m_capabilities.contains("CONDSTORE"_L1, Qt::CaseInsensitive)) {
        const QString featureTag = nextTag();
        const QString featureCmd = m_capabilities.contains("QRESYNC"_L1, Qt::CaseInsensitive)
            ? "ENABLE QRESYNC"_L1
            : "ENABLE CONDSTORE"_L1;
        m_socket->write(buildSimpleCommand(featureTag, featureCmd));
        m_socket->flush();
        (void)IO::readUntilTagged(*m_socket, featureTag, IO::kFetchReadTimeoutMs);
    }

    // 2) NAMESPACE negotiation (or LIST "" "" delimiter fallback)
    if (m_capabilities.contains("NAMESPACE"_L1, Qt::CaseInsensitive)) {
        const QString nsTag = nextTag();
        m_socket->write(buildSimpleCommand(nsTag, "NAMESPACE"_L1));
        m_socket->flush();
        (void)IO::readUntilTagged(*m_socket, nsTag, IO::kFetchReadTimeoutMs);
    } else {
        const QString listTag = nextTag();
        m_socket->write(buildSimpleCommand(listTag, R"(LIST "" "")"_L1));
        m_socket->flush();
        (void)IO::readUntilTagged(*m_socket, listTag, IO::kFetchReadTimeoutMs);
    }

    result.success      = true;
    result.message      = QStringLiteral("Connected and authenticated");
    result.capabilities = m_capabilities;

    return result;
}

QString
Connection::execute(const QString &command) {
    const QString tag = nextTag();
    m_socket->write(buildSimpleCommand(tag, command));
    m_socket->flush();

    QString imapResp = IO::readUntilTagged(*m_socket, tag, IO::kFetchReadTimeoutMs);
    if (!imapResp.contains(tag + " OK"_L1, Qt::CaseInsensitive)) {
        return "%1 failed: %2"_L1.arg(command).arg(imapResp.simplified().left(200));
    }

    return imapResp;
}

QByteArray
Connection::executeRaw(const QString &command) {
    const QString tag = nextTag();
    m_socket->write(buildSimpleCommand(tag, command));
    m_socket->flush();
    return IO::readUntilTaggedRaw(*m_socket, tag, IO::kFetchReadTimeoutMs);
}

QVariantList
Connection::list() {
    if (!isConnected())
        return {};

    const QString tag = nextTag();
    const QString command = isGmail() ? R"(XLIST "" "*")"_L1 : R"(LIST "" "*")"_L1;
    m_socket->write(buildSimpleCommand(tag, command));
    m_socket->flush();

    auto resp = IO::readUntilTagged(*m_socket, tag, IO::kFetchReadTimeoutMs);
    if (!resp.contains(tag + " OK"_L1, Qt::CaseInsensitive) && isGmail()) {
        const QString fallbackTag = nextTag();
        m_socket->write(buildSimpleCommand(fallbackTag, R"(LIST "" "*")"_L1));
        m_socket->flush();
        resp = IO::readUntilTagged(*m_socket, fallbackTag, IO::kFetchReadTimeoutMs);
        if (!resp.contains(fallbackTag + " OK"_L1, Qt::CaseInsensitive))
            return {};
    } else if (!resp.contains(tag + " OK"_L1, Qt::CaseInsensitive)) {
        return {};
    }

    return parseListResponse(resp);
}

std::tuple<bool, QString>
Connection::select(const QString &mailbox) {
    const QString tag = nextTag();
    m_socket->write(buildSelectCommand(tag, mailbox));
    m_socket->flush();

    const QString resp = IO::readUntilTagged(*m_socket, tag, IO::kFetchReadTimeoutMs);
    if (const bool ok = resp.contains(tag + " OK"_L1, Qt::CaseInsensitive); !ok) {
        return {false, resp};
    }

    m_selectedFolder = mailbox;
    return {true, resp};
}

std::tuple<bool, QString>
Connection::enterIdle() {
    if (!isConnected())
        return {false, "IDLE failed: not connected"_L1};

    const QString tag = nextTag();
    m_socket->write(buildSimpleCommand(tag, "IDLE"_L1));
    m_socket->flush();

    if (!m_socket->waitForReadyRead(IO::kReadTimeoutMs))
        return {false, "IDLE failed: timeout waiting for continuation"_L1};

    const QString resp = QString::fromUtf8(m_socket->readAll());
    if (!resp.contains('+'))
        return {false, "IDLE failed: server rejected IDLE: %1"_L1.arg(resp.simplified().left(200))};

    m_idleTag = tag;
    return {true, resp};
}

QString
Connection::waitForIdlePush(const int timeoutMs) const {
    if (m_idleTag.isEmpty() || !m_socket)
        return {};

    if (!m_socket->waitForReadyRead(timeoutMs))
        return {};

    return QString::fromUtf8(m_socket->readAll());
}

std::tuple<bool, QString>
Connection::exitIdle() {
    if (m_idleTag.isEmpty() || !m_socket)
        return {false, "IDLE failed: not in IDLE mode"_L1};

    m_socket->write("DONE\r\n");
    m_socket->flush();

    const QString doneTag = m_idleTag;
    m_idleTag.clear();
    const QString resp = IO::readUntilTagged(*m_socket, doneTag, IO::kTaggedReadTimeoutMs);
    const bool ok = resp.contains(doneTag + " OK"_L1, Qt::CaseInsensitive);
    return {ok, resp};
}

void
Connection::disconnect() {
    if (!m_socket || !m_socket->isOpen()) {
        return;
    }

    if (m_authenticated) {
        m_socket->write(buildSimpleCommand(nextTag(), QStringLiteral("LOGOUT")));
        m_socket->flush();
        // Don't wait for response — best effort only
    }

    m_socket->disconnectFromHost();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->waitForDisconnected(1000);
    }

    m_authenticated = false;
    m_idleTag.clear();
}

bool
Connection::isConnected() const {
    return m_socket && m_socket->isOpen() && m_authenticated;
}

QString
Connection::nextTag() {
    return QString("a%1").arg(m_tag++, 3, 10, QLatin1Char('0'));
}

} // namespace Imap
