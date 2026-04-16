#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QThread;

namespace Imap {
class IdleWatcher;
class BackgroundWorker;
}

/**
 * Abstract interface for a mail account.
 *
 * The UI binds to this interface without knowing whether the underlying
 * implementation is Gmail (OAuth, categories, labels), generic IMAP
 * (password, standard folders), or POP3 (future).
 *
 * Each account owns its own IDLE watcher and background worker threads.
 */
class IAccount : public QObject
{
    Q_OBJECT

    // ── Identity ──────────────────────────────────────────────────
    Q_PROPERTY(QString email READ email CONSTANT)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY accountNameChanged)
    Q_PROPERTY(QString avatarSource READ avatarSource NOTIFY avatarSourceChanged)
    Q_PROPERTY(QString providerIcon READ providerIcon CONSTANT)
    Q_PROPERTY(QString providerId READ providerId CONSTANT)

    // ── Folders & Tags ────────────────────────────────────────────
    Q_PROPERTY(QVariantList folderList READ folderList NOTIFY foldersChanged)
    Q_PROPERTY(QVariantList tagList READ tagList NOTIFY tagsChanged)
    Q_PROPERTY(QStringList categoryTabs READ categoryTabs NOTIFY categoryTabsChanged)

    // ── Status ────────────────────────────────────────────────────
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool throttled READ throttled NOTIFY throttledChanged)
    Q_PROPERTY(bool needsReauth READ needsReauth NOTIFY needsReauthChanged)

public:
    explicit IAccount(QObject *parent = nullptr) : QObject(parent) {}

    // ── Identity ──────────────────────────────────────────────────
    [[nodiscard]] virtual QString email() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual QString accountName() const = 0;
    [[nodiscard]] virtual QString avatarSource() const = 0;
    [[nodiscard]] virtual QString providerIcon() const = 0;
    [[nodiscard]] virtual QString providerId() const = 0;

    // ── Folders & Tags ────────────────────────────────────────────
    [[nodiscard]] virtual QVariantList folderList() const = 0;
    [[nodiscard]] virtual QVariantList tagList() const = 0;
    [[nodiscard]] virtual QStringList categoryTabs() const = 0;
    Q_INVOKABLE virtual QVariantMap folderStats(const QString &folderKey) const = 0;

    // ── Sync targets ─────────────────────────────────────────────
    [[nodiscard]] virtual QStringList syncTargets() const = 0;

    // ── Sync ──────────────────────────────────────────────────────
    Q_INVOKABLE virtual void syncAll() = 0;
    Q_INVOKABLE virtual void syncFolder(const QString &folderName) = 0;
    Q_INVOKABLE virtual void refreshFolderList() = 0;

    // ── Connection ────────────────────────────────────────────────
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    Q_INVOKABLE virtual void reauthenticate() = 0;

    // ── Status ────────────────────────────────────────────────────
    [[nodiscard]] virtual bool connected() const = 0;
    [[nodiscard]] virtual bool syncing() const = 0;
    [[nodiscard]] virtual bool throttled() const = 0;
    [[nodiscard]] virtual bool needsReauth() const = 0;

signals:
    void displayNameChanged();
    void accountNameChanged();
    void avatarSourceChanged();
    void foldersChanged();
    void tagsChanged();
    void categoryTabsChanged();
    void connectedChanged();
    void syncingChanged();
    void throttledChanged();
    void needsReauthChanged();
    void syncFinished(bool ok, const QString &message);
    void bodyHtmlUpdated(const QString &folder, const QString &uid);
    void newMailReceived(const QVariantMap &info);
};
