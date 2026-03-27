# Kestrel Mail Privacy Policy

**Last updated:** March 27, 2026

## Overview

Kestrel Mail is an open-source desktop email client. It runs entirely on your computer and does not operate any servers, cloud services, or analytics infrastructure.

## Data Collection

Kestrel Mail does **not** collect, transmit, or store any user data on external servers. All data remains on your local machine.

## Data Stored Locally

The following data is stored locally on your computer to provide email functionality:

- **Email messages**: Headers, bodies, and attachments synced from your IMAP server are cached in a local SQLite database.
- **Account credentials**: OAuth2 refresh tokens are stored in a local file (`~/.local/share/kestrel-mail/tokens.json`). Access tokens are obtained on-demand and held only in memory.
- **Account configuration**: Email account settings (server addresses, display names) are stored locally.
- **Contact information**: Sender names, email addresses, and avatar URLs are cached locally for display purposes.
- **Search history**: Recent search queries are stored locally for convenience.
- **User preferences**: Application settings, trusted sender domains, and folder configurations are stored locally.

## Third-Party Services

Kestrel Mail communicates with the following external services as part of normal email client operation:

- **Your email provider's IMAP and SMTP servers** (e.g., Gmail, Outlook) to sync and send email.
- **Your email provider's OAuth2 endpoints** (e.g., Google's token endpoint) to authenticate your account.
- **Google People API** (for Gmail accounts only) to retrieve contact photos.

No data is sent to the Kestrel Mail developers, or any other third party.

## Google API Services

Kestrel Mail's use of Google API services adheres to the [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy), including the Limited Use requirements:

- Kestrel Mail only requests the minimum scopes necessary to provide email functionality (`https://mail.google.com/` for IMAP/SMTP access).
- Data obtained from Google APIs is used solely to provide email functionality to you.
- Data is not transferred to third parties except as necessary to provide the email service.
- Data is not used for advertising or any purpose unrelated to email functionality.
- Human access to your data is limited to debugging at your explicit request.

## Data Security

- OAuth2 refresh tokens are stored in a plain-text JSON file with standard filesystem permissions. Future versions may integrate with platform-specific keychains (e.g., KWallet).
- All IMAP and SMTP connections use TLS encryption.
- No data leaves your computer except to communicate with your configured email provider.

## Data Deletion

Uninstalling Kestrel Mail and removing the following directories will delete all locally stored data:

- `~/.local/share/kestrel-mail/` (database, tokens, account config)
- `~/.config/kestrel-mail/` (application settings, if present)

## Children's Privacy

Kestrel Mail does not knowingly collect any information from children under 13.

## Changes to This Policy

Changes to this policy will be reflected in the application's source repository. The "Last updated" date at the top of this document indicates when the policy was last modified.

## Contact

For questions about this privacy policy or Kestrel Mail's data practices, please open an issue on the [GitHub repository](https://github.com/lenonk/kestrel-mail).
