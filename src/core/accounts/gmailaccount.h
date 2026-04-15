#pragma once

#include "baseaccount.h"

/**
 * Gmail account implementation.
 *
 * Uses XOAUTH2 authentication, Gmail-specific folder structure
 * ([Gmail]/All Mail, categories), label-based tags, and integrates
 * with Google Calendar and Contacts APIs.
 *
 * Extends BaseAccount with:
 *  - throttle signal wiring (accountThrottled / accountUnthrottled)
 *  - Google People avatar refresh on syncAll()
 *  - Gmail-specific avatar icon
 *  - inbox category tabs from DataStore
 */
class GmailAccount : public BaseAccount
{
    Q_OBJECT

public:
    explicit GmailAccount(const QVariantMap &config, DataStore *store,
                           ImapService *imap, TokenVault *vault,
                           QObject *parent = nullptr);

    // -- Overrides ------------------------------------------------------------
    [[nodiscard]] QString avatarSource() const override;
    [[nodiscard]] QStringList categoryTabs() const override;
    Q_INVOKABLE void syncAll() override;
};
