#pragma once

#include "iaccount.h"
#include <QVariantMap>

class QThread;
class QTimer;
class DataStore;
class ImapService;
class TokenVault;

namespace Imap {
class IdleWatcher;
class BackgroundWorker;
}

/**
 * Generic IMAP account implementation.
 *
 * Uses LOGIN (password) authentication, standard IMAP folder hierarchy,
 * no Gmail-specific features (no categories, no labels).
 */
class ImapAccount : public IAccount
{
    Q_OBJECT

public:
    explicit ImapAccount(const QVariantMap &config, DataStore *store,
                          ImapService *imap, TokenVault *vault,
                          QObject *parent = nullptr);
    ~ImapAccount() override;

    // ── Identity ──────────────────────────────────────────────────
    [[nodiscard]] QString email() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString accountName() const override;
    [[nodiscard]] QString avatarSource() const override;
    [[nodiscard]] QString providerIcon() const override;

    // ── Folders & Tags ────────────────────────────────────────────
    [[nodiscard]] QVariantList folderList() const override;
    [[nodiscard]] QVariantList tagList() const override;
    [[nodiscard]] QStringList categoryTabs() const override;
    Q_INVOKABLE QVariantMap folderStats(const QString &folderKey) const override;

    // ── Sync targets ─────────────────────────────────────────────
    [[nodiscard]] QStringList syncTargets() const override;

    // ── Sync ──────────────────────────────────────────────────────
    Q_INVOKABLE void syncAll() override;
    Q_INVOKABLE void syncFolder(const QString &folderName) override;
    Q_INVOKABLE void refreshFolderList() override;

    // ── Connection ────────────────────────────────────────────────
    void initialize() override;
    void shutdown() override;
    Q_INVOKABLE void reauthenticate() override;

    // ── Status ────────────────────────────────────────────────────
    [[nodiscard]] bool connected() const override;
    [[nodiscard]] bool syncing() const override;
    [[nodiscard]] bool throttled() const override;
    [[nodiscard]] bool needsReauth() const override;

private:
    void startIdleWatcher();
    void stopIdleWatcher();
    void startBackgroundWorker();
    void stopBackgroundWorker();
    void updateSyncState(bool active);

    QVariantMap m_config;
    DataStore *m_store;
    ImapService *m_imap;
    TokenVault *m_vault;
    QString m_email;

    Imap::IdleWatcher *m_idleWatcher = nullptr;
    QThread *m_idleThread = nullptr;
    Imap::BackgroundWorker *m_bgWorker = nullptr;
    QThread *m_bgThread = nullptr;
    QTimer *m_syncTimer = nullptr;

    bool m_syncing = false;
    int m_syncCount = 0;
    bool m_connected = true;
    bool m_throttled = false;
    bool m_needsReauth = false;
};
