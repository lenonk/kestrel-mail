# Kestrel Mail Privacy Policy

**Last updated:** April 8, 2026

## Overview

Kestrel Mail is an open-source desktop email client. It runs entirely on your computer and does not operate any servers, cloud services, or analytics infrastructure.

## Data Collection

Kestrel Mail does **not** collect, transmit, or store any user data on external servers. All data remains on your local machine.

## Data Stored Locally

The following data is stored locally on your computer to provide email, calendar, and contact functionality:

- **Email messages**: Headers, bodies, and attachments synced from your IMAP server are cached in a local SQLite database.
- **Local folder messages**: Messages copied to local folders are stored permanently with their full body and attachments in a local directory.
- **Calendar events**: Events fetched from Google Calendar are held in memory for display and are not persisted to disk.
- **Contact information**: Sender names, email addresses, avatar URLs, and contacts synced from Google are cached locally for display purposes.
- **Account credentials**: OAuth2 refresh tokens are stored securely using KWallet (KDE), libsecret (GNOME), or a local file as a last resort. Access tokens are obtained on-demand and held only in memory.
- **Account configuration**: Email account settings (server addresses, display names) are stored locally.
- **Search history**: Recent search queries are stored locally for convenience.
- **User preferences**: Application settings, trusted sender domains, and folder configurations are stored locally.

## Third-Party Services

Kestrel Mail communicates with the following external services as part of normal operation:

- **Your email provider's IMAP and SMTP servers** (e.g., Gmail, Outlook) to sync and send email.
- **Your email provider's OAuth2 endpoints** (e.g., Google's token endpoint) to authenticate your account.
- **Google Calendar API** (for Gmail accounts) to fetch calendar events and respond to invitations.
- **Google People API** (for Gmail accounts) to retrieve and sync contacts and contact photos.

No data is sent to the Kestrel Mail developers, or any other third party.

## Google API Services

Kestrel Mail's use of Google API services adheres to the [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy), including the Limited Use requirements:

- Kestrel Mail requests the following scopes:
  - `https://mail.google.com/` — IMAP and SMTP access for reading and sending email.
  - `https://www.googleapis.com/auth/calendar` — Reading calendar events and responding to invitations.
  - `https://www.googleapis.com/auth/contacts` — Reading and syncing contacts.
- Data obtained from Google APIs is used solely to provide email, calendar, and contact functionality to you.
- Data is not transferred to third parties except as necessary to provide the service.
- Data is not used for advertising or any purpose unrelated to email, calendar, or contact functionality.
- Human access to your data is limited to debugging at your explicit request.

## Data Security

- OAuth2 refresh tokens are stored using KWallet (KDE), libsecret (GNOME Keyring), or a plain-text JSON file with standard filesystem permissions as a last resort.
- All IMAP and SMTP connections use TLS encryption.
- All Google API requests use HTTPS.
- No data leaves your computer except to communicate with your configured email provider and the Google APIs listed above.

## Data Deletion

Uninstalling Kestrel Mail and removing the following directories will delete all locally stored data:

- `~/.local/share/kestrel-mail/` (database, local folder messages, attachments, account config)
- `~/.config/kestrel-mail/` (application settings, if present)
- `~/.cache/kestrel-mail/` (cached attachments)

To remove stored tokens, use your platform's keychain manager (KWallet, Seahorse, etc.) or delete `~/.local/share/kestrel-mail/tokens.json` if using the file fallback.

## Children's Privacy

Kestrel Mail does not knowingly collect any information from children under 13.

## Changes to This Policy

Changes to this policy will be reflected in the application's source repository. The "Last updated" date at the top of this document indicates when the policy was last modified.

## Contact

For questions about this privacy policy or Kestrel Mail's data practices, please open an issue on the [GitHub repository](https://github.com/lenonk/kestrel-mail).
