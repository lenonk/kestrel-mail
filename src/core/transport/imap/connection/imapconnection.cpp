#include "imapconnection.h"
#include "imapio.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>

using namespace Qt::Literals::StringLiterals;

namespace Imap {
Connection::ThrottleObserver Connection::s_throttleObserver = {};

namespace {

bool responseIsThrottled(const QString &resp) {
    return resp.contains("THROTTLED"_L1, Qt::CaseInsensitive)
        || resp.contains("RATE"_L1 + " "_L1 + "LIMIT"_L1, Qt::CaseInsensitive)
        || resp.contains("TOO MANY REQUESTS"_L1, Qt::CaseInsensitive);
}

QMutex &imapLogMutex() {
    static QMutex m;
    return m;
}

QString elideMiddle(const QString &s, const int maxChars = 2400) {
    if (s.size() <= maxChars)
        return s;
    const int keep = maxChars / 2;
    return s.left(keep) + "\n...<ELIDED>...\n" + s.right(keep);
}

void appendImapLog(const QString &email, const QString &command, const QString &response) {
    QMutexLocker lock(&imapLogMutex());
    QFile f("imap-traffic.log");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString cmd = elideMiddle(command.trimmed());
    const QString resp = elideMiddle(response.trimmed());
    const QByteArray block = QStringLiteral(
        "[%1] account=%2\nC: %3\nS: %4\n----\n")
        .arg(ts, email, cmd, resp).toUtf8();
    f.write(block);
}

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
        else if (flags.contains("\\Spam"_L1,   Qt::CaseInsensitive)) specialUse = "junk"_L1;
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
    m_port        = port;
    m_email       = email;
    m_accessToken = accessToken;
    m_idleTag.clear();
    m_selectedFolder.clear();

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
    const QString authCommand = QStringLiteral("AUTHENTICATE XOAUTH2 <base64>");
    m_socket->write(buildXOAuth2Command(authTag, email, accessToken));
    m_socket->flush();

    auto imapResp = IO::readUntilTagged(*m_socket, authTag, IO::kTaggedReadTimeoutMs);
    observeThrottleState(imapResp);

    // Handle SASL continuation if present
    if (imapResp.contains("\r\n+ "_L1) || imapResp.startsWith("+ "_L1)) {
        m_socket->write("\r\n");
        m_socket->flush();
        imapResp += IO::readUntilTagged(*m_socket, authTag, IO::kTaggedReadTimeoutMs);
        observeThrottleState(imapResp);
    }

    appendImapLog(email, authCommand, imapResp);

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
    observeThrottleState(imapResp);
    appendImapLog(email, QStringLiteral("CAPABILITY"), imapResp);
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
    observeThrottleState(imapResp);
    appendImapLog(email, QStringLiteral("ENABLE UTF8=ACCEPT"), imapResp);
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
        const QString featureResp = IO::readUntilTagged(*m_socket, featureTag, IO::kFetchReadTimeoutMs);
        observeThrottleState(featureResp);
        appendImapLog(email, featureCmd, featureResp);
    }

    // 2) NAMESPACE negotiation (or LIST "" "" delimiter fallback)
    if (m_capabilities.contains("NAMESPACE"_L1, Qt::CaseInsensitive)) {
        const QString nsTag = nextTag();
        m_socket->write(buildSimpleCommand(nsTag, "NAMESPACE"_L1));
        m_socket->flush();
        const QString nsResp = IO::readUntilTagged(*m_socket, nsTag, IO::kFetchReadTimeoutMs);
        observeThrottleState(nsResp);
        appendImapLog(email, QStringLiteral("NAMESPACE"), nsResp);
    } else {
        const QString listTag = nextTag();
        m_socket->write(buildSimpleCommand(listTag, R"(LIST "" "")"_L1));
        m_socket->flush();
        const QString listResp = IO::readUntilTagged(*m_socket, listTag, IO::kFetchReadTimeoutMs);
        observeThrottleState(listResp);
        appendImapLog(email, QStringLiteral("LIST \"\" \"\""), listResp);
    }

    result.success      = true;
    result.message      = QStringLiteral("Connected and authenticated");
    result.capabilities = m_capabilities;

    // Detach socket from its creating thread so it can be used from any thread
    // without QSocketNotifier warnings. We use synchronous waitFor* calls only,
    // so no event loop delivery is needed.
    m_socket->moveToThread(nullptr);

    return result;
}

void
Connection::setThrottleObserver(Connection::ThrottleObserver observer) {
    s_throttleObserver = std::move(observer);
}

void
Connection::observeThrottleState(const QString &response) {
    const bool seenThrottled = responseIsThrottled(response);
    if (seenThrottled == m_throttled)
        return;

    m_throttled = seenThrottled;
    if (s_throttleObserver)
        s_throttleObserver(m_email, m_throttled, response);
}

QString
Connection::execute(const QString &command) {
    const QString tag = nextTag();
    m_socket->write(buildSimpleCommand(tag, command));
    m_socket->flush();

    QString imapResp = IO::readUntilTagged(*m_socket, tag, IO::kFetchReadTimeoutMs);
    observeThrottleState(imapResp);
    appendImapLog(m_email, command, imapResp);
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
    const QByteArray raw = IO::readUntilTaggedRaw(*m_socket, tag, IO::kFetchReadTimeoutMs);
    const QString respText = QString::fromUtf8(raw);
    observeThrottleState(respText);
    appendImapLog(m_email, command, respText);
    return raw;
}

QByteArray
Connection::fetchMimePartWithProgress(const QString &uid,
                                      const QString &partSpecifier,
                                      const int progressStepPercent,
                                      const std::function<void(int, qint64)> &onProgress,
                                      QString *statusOut) {
    if (!isConnected()) {
        if (statusOut) *statusOut = QStringLiteral("Not connected");
        return {};
    }

    const QString tag = nextTag();
    const QString command = QStringLiteral("UID FETCH %1 (BODY.PEEK[%2])").arg(uid, partSpecifier);
    m_socket->write(buildSimpleCommand(tag, command));
    m_socket->flush();

    QByteArray acc;
    QByteArray literal;
    qint64 literalLen = -1;
    qint64 payloadStart = -1;
    bool haveLiteral = false;
    int lastPercent = -1;
    const int step = qBound(1, progressStepPercent, 100);

    static const QRegularExpression literalRe(QStringLiteral("\\{(\\d+)\\}\\r\\n"));

    while (m_socket->waitForReadyRead(IO::kFetchReadTimeoutMs)) {
        acc += m_socket->readAll();

        if (!haveLiteral) {
            const QString accText = QString::fromLatin1(acc);
            const auto m = literalRe.match(accText);
            if (m.hasMatch()) {
                literalLen = m.captured(1).toLongLong();
                payloadStart = m.capturedEnd(0);
                haveLiteral = (literalLen >= 0 && payloadStart >= 0);
            }
        }

        if (haveLiteral && literalLen >= 0 && payloadStart >= 0) {
            const qint64 available = qMax<qint64>(0, acc.size() - payloadStart);
            const qint64 desired = qMin<qint64>(literalLen, available);
            if (desired > literal.size()) {
                const qint64 appendFrom = payloadStart + literal.size();
                const qint64 appendLen = desired - literal.size();
                literal += acc.mid(static_cast<int>(appendFrom), static_cast<int>(appendLen));
            }
        }

        if (haveLiteral && literalLen > 0 && onProgress) {
            const qint64 literalRead = literal.size();
            const int percent = qBound(0, static_cast<int>((literalRead * 100) / literalLen), 100);
            const bool crossedStep = (lastPercent < 0) || (percent - lastPercent >= step);
            const bool reachedEnd = (percent == 100) && (lastPercent < 100);
            if (percent > lastPercent && (crossedStep || reachedEnd)) {
                lastPercent = percent;
                onProgress(percent, literalRead);
            }
        }

        if (acc.contains((tag + " OK"_L1).toUtf8())
            || acc.contains((tag + " NO"_L1).toUtf8())
            || acc.contains((tag + " BAD"_L1).toUtf8())) {
            if (statusOut) *statusOut = QString::fromUtf8(acc);
            break;
        }
    }

    const QString finalResp = QString::fromUtf8(acc);
    observeThrottleState(finalResp);
    appendImapLog(m_email, command, finalResp);

    if (haveLiteral)
        return literal;

    if (statusOut) *statusOut = finalResp;
    return {};
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
    observeThrottleState(resp);
    appendImapLog(m_email, command, resp);
    if (!resp.contains(tag + " OK"_L1, Qt::CaseInsensitive) && isGmail()) {
        const QString fallbackTag = nextTag();
        m_socket->write(buildSimpleCommand(fallbackTag, R"(LIST "" "*")"_L1));
        m_socket->flush();
        resp = IO::readUntilTagged(*m_socket, fallbackTag, IO::kFetchReadTimeoutMs);
        observeThrottleState(resp);
        appendImapLog(m_email, QStringLiteral("LIST \"\" \"*\""), resp);
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
    observeThrottleState(resp);
    appendImapLog(m_email, QStringLiteral("SELECT \"%1\"").arg(mailbox), resp);
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

    if (!m_socket->waitForReadyRead(IO::kIdleContinuationTimeoutMs))
        return {false, "IDLE failed: timeout waiting for continuation"_L1};

    const QString resp = QString::fromUtf8(m_socket->readAll());
    appendImapLog(m_email, QStringLiteral("IDLE"), resp);
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
    observeThrottleState(resp);
    appendImapLog(m_email, QStringLiteral("DONE"), resp);
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
    m_selectedFolder.clear();
}

bool
Connection::isConnected() const {
    return m_socket
        && m_socket->state() == QAbstractSocket::ConnectedState
        && m_authenticated;
}

bool
Connection::tryReconnect() {
    if (m_host.isEmpty() || m_email.isEmpty() || m_accessToken.isEmpty())
        return false;
    return connectAndAuth(m_host, m_port, m_email, m_accessToken).success;
}

QString
Connection::nextTag() {
    return QString("a%1").arg(m_tag++, 3, 10, QLatin1Char('0'));
}

} // namespace Imap
