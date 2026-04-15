#pragma once

#include "baseaccount.h"

/**
 * Generic IMAP account implementation.
 *
 * Uses LOGIN (password) authentication, standard IMAP folder hierarchy,
 * no Gmail-specific features (no categories, no labels).
 *
 * Everything is inherited from BaseAccount; this subclass exists so the
 * type system distinguishes generic IMAP from provider-specific accounts.
 */
class ImapAccount : public BaseAccount
{
    Q_OBJECT

public:
    using BaseAccount::BaseAccount;
};
