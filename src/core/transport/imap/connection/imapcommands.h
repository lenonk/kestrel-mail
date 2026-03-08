#pragma once

#include <QString>
#include <QByteArray>

namespace ImapCommands {

/**
 * Build XOAUTH2 authentication command.
 * Format: TAG AUTHENTICATE XOAUTH2 <base64(user=EMAIL\x01auth=Bearer TOKEN\x01\x01)>
 * 
 * @param tag Command tag (e.g., "a001")
 * @param email User email address
 * @param accessToken OAuth2 access token
 * @return Complete AUTHENTICATE command with CRLF
 */
QByteArray buildXOAuth2Command(const QString &tag, const QString &email, const QString &accessToken);

/**
 * Build UID SEARCH command.
 * 
 * @param tag Command tag
 * @param criteria Search criteria (e.g., "ALL", "UID 1000:*")
 * @return Complete UID SEARCH command with CRLF
 */
QByteArray buildUidSearchCommand(const QString &tag, const QString &criteria);

/**
 * Build UID FETCH command.
 * 
 * @param tag Command tag
 * @param uidSet UID set (e.g., "1:100", "1,5,10")
 * @param items Items to fetch (e.g., "FLAGS BODY.PEEK[]")
 * @return Complete UID FETCH command with CRLF
 */
QByteArray buildUidFetchCommand(const QString &tag, const QString &uidSet, const QString &items);

/**
 * Build SELECT command.
 * 
 * @param tag Command tag
 * @param mailbox Mailbox name (will be quoted if needed)
 * @return Complete SELECT command with CRLF
 */
QByteArray buildSelectCommand(const QString &tag, const QString &mailbox);

/**
 * Build simple command (CAPABILITY, LOGOUT, etc.)
 * 
 * @param tag Command tag
 * @param command Command name (e.g., "CAPABILITY", "LOGOUT")
 * @return Complete command with CRLF
 */
QByteArray buildSimpleCommand(const QString &tag, const QString &command);

} // namespace ImapCommands
