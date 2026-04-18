#include <memory>

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QWindow>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>

#include "core/accounts/iaccount.h"
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPainter>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "ui/splashscreen.h"
#include "ui/calendarlayouthelper.h"
#include "core/htmlprocessor.h"
#include "core/accounts/accountrepository.h"
#include "core/accounts/accountsetupcontroller.h"
#include "core/accounts/providerprofileservice.h"
#include "core/auth/filetokenvault.h"
#include "core/auth/kwallettokenvault.h"
#ifdef HAVE_LIBSECRET
#include "core/auth/libsecrettokenvault.h"
#endif
#include <KWallet>
#include "core/auth/oauthservice.h"
#include "core/store/datastore.h"
#include "core/store/messagelistmodel.h"
#include "core/transport/imap/imapservice.h"
#include "core/transport/imap/googleapiservice.h"
#include "core/transport/smtp/smtpservice.h"
#include "core/crypto/pgpkeymanager.h"
#include "core/accounts/accountmanager.h"
#include <KNotification>

using namespace Qt::Literals::StringLiterals;

class CachingNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new QNetworkAccessManager(parent);
        auto *diskCache = new QNetworkDiskCache(nam);

        const QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString cachePath = cacheBase + "/http"_L1;
        QDir().mkpath(cachePath);
        diskCache->setCacheDirectory(cachePath);
        diskCache->setMaximumCacheSize(128LL * 1024LL * 1024LL); // 128MB

        nam->setCache(diskCache);
        return nam;
    }
};

// Watches QExposeEvent on a QWindow and emits windowReExposed() whenever the
// surface transitions from hidden → visible.  On Wayland the compositor
// destroys the surface when the window moves off-screen (minimize, desktop
// switch, activity switch); Chromium's GPU compositor never recovers, so QML
// WebEngineViews listen for this signal to force-reload their content.
class WindowExposeWatcher : public QObject
{
    Q_OBJECT
public:
    explicit WindowExposeWatcher(QWindow *window, QObject *parent = nullptr)
        : QObject(parent), m_window(window), m_wasExposed(window->isExposed())
    {
        window->installEventFilter(this);
    }

Q_SIGNALS:
    void windowReExposed();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == m_window && event->type() == QEvent::Expose) {
            const bool exposed = m_window->isExposed();
            if (exposed && !m_wasExposed)
                Q_EMIT windowReExposed();
            m_wasExposed = exposed;
        }
        return false;
    }

private:
    QWindow *m_window;
    bool m_wasExposed;
};

int main(int argc, char *argv[])
{
    QtWebEngineQuick::initialize();
    QApplication app(argc, argv);
    app.setApplicationName("kestrel-mail"_L1);
    app.setOrganizationDomain("github.com"_L1);
    app.setOrganizationName("lenonk"_L1);
    const QIcon kestrelIcon(":/data/assets/kestrel-mail.png"_L1);
    app.setWindowIcon(kestrelIcon.isNull()
                          ? QIcon::fromTheme("mail-message-new"_L1)
                          : kestrelIcon);

    KLocalizedString::setApplicationDomain("kestrel-mail");

    std::unique_ptr<SplashScreen> splash;
    {
        QPixmap splashPixmap(":/data/assets/splash.png"_L1);
        if (!splashPixmap.isNull()) {
            splash = std::make_unique<SplashScreen>(splashPixmap);
            splash->show();
            app.processEvents();
        }
    }

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new CachingNetworkAccessManagerFactory());
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));

    ProviderProfileService providerProfiles(&engine);
    // Token storage: prefer KWallet, then libsecret (GNOME Keyring), then plaintext file.
    std::unique_ptr<TokenVault> tokenVault;
    if (KWallet::Wallet::isEnabled()) {
        tokenVault = std::make_unique<KWalletTokenVault>();
        qInfo() << "[token-vault] Using KWallet backend";
    }
#ifdef HAVE_LIBSECRET
    else {
        tokenVault = std::make_unique<LibSecretTokenVault>();
        qInfo() << "[token-vault] Using libsecret (GNOME Keyring) backend";
    }
#else
    else {
        tokenVault = std::make_unique<FileTokenVault>();
        qInfo() << "[token-vault] Using plaintext file backend";
    }
#endif
    OAuthService oauthService(tokenVault.get(), &engine);
    DataStore dataStore(&engine);
    dataStore.init();
    dataStore.quickCheck();
    AccountRepository accountRepository(&dataStore, &engine);
    AccountSetupController accountSetup(&providerProfiles, &oauthService, &accountRepository, tokenVault.get(), &engine);
    MessageListModel messageListModel(&engine);
    messageListModel.setDataStore(&dataStore);
    HtmlProcessor htmlProcessor(&engine);
    ImapService imapService(&dataStore, tokenVault.get(), &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &imapService, &ImapService::shutdown);
    GoogleApiService googleApiService(
        [&imapService]() { return imapService.accountConfigList(); },
        [&imapService](const QVariantMap &acc, const QString &email) {
            return imapService.refreshAccessToken(acc, email);
        },
        [&tokenVault](const QString &email) {
            return tokenVault ? tokenVault->loadPassword(email) : QString();
        },
        &dataStore, &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &googleApiService, &GoogleApiService::shutdown);
    // Pool initialization is handled per-account by AccountManager::createAccount().
    SmtpService smtpService(&accountRepository, tokenVault.get(), &engine);
    PgpKeyManager pgpKeyManager(&dataStore, &engine);
    AccountManager accountManager(&accountRepository, &dataStore, &imapService, &googleApiService, tokenVault.get(), &engine);

    // Desktop notification for new mail.
    QObject::connect(&dataStore, &DataStore::newMailReceived, &app,
        [&dataStore, &imapService, &accountManager](const QVariantMap &info) {
            const QString senderRaw = info.value("senderRaw"_L1).toString();
            const QString subject   = info.value("subject"_L1).toString();
            const QString snippet   = info.value("snippet"_L1).toString();
            const QString account   = info.value("accountEmail"_L1).toString();
            const QString folder    = info.value("folder"_L1).toString();
            const QString uid       = info.value("uid"_L1).toString();

            // Resolve display name through the same path the message list uses:
            // contacts DB lookup → extracted display name → bare email fallback.
            const int lt = senderRaw.indexOf('<');
            const int gt = senderRaw.indexOf('>', lt);
            const QString email = (lt >= 0 && gt > lt)
                ? senderRaw.mid(lt + 1, gt - lt - 1).trimmed()
                : senderRaw.trimmed();
            QString sender = dataStore.displayNameForEmail(email);
            if (sender.isEmpty())
                sender = info.value("senderDisplay"_L1).toString();

            auto *n = new KNotification("newMail"_L1);
            n->setTitle(sender);

            QString body = subject;
            if (!snippet.isEmpty())
                body += "\n"_L1 + snippet;
            QString acctName = account;
            if (auto *acct = qobject_cast<IAccount*>(accountManager.accountByEmail(account)))
                acctName = acct->accountName();
            body += "\nvia "_L1 + acctName;
            n->setText(body);

            // Load sender avatar or generate initials fallback.
            const QString avatarUrl = dataStore.avatarForEmail(email);
            QPixmap avatar;
            if (!avatarUrl.isEmpty()) {
                QString path = avatarUrl;
                if (path.startsWith("file://"_L1))
                    path = path.mid(7);
                QPixmap raw(path);
                if (!raw.isNull()) {
                    raw = raw.scaled(64, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    avatar = QPixmap(64, 64);
                    avatar.fill(Qt::transparent);
                    QPainter p(&avatar);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setBrush(QBrush(raw.copy((raw.width() - 64) / 2, (raw.height() - 64) / 2, 64, 64)));
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(0, 0, 64, 64);
                }
            }
            if (avatar.isNull())
                avatar = DataStore::avatarPixmap(sender, senderRaw);
            n->setPixmap(avatar);
            n->setHint("sound-name"_L1, "message-new-instant"_L1);

            auto *markRead = n->addAction("Mark as Read"_L1);
            auto *reply    = n->addAction("Reply"_L1);

            QObject::connect(markRead, &KNotificationAction::activated, [&imapService, &dataStore, account, folder, uid]() {
                imapService.markMessageRead(account, folder, uid);
                dataStore.markMessageRead(account, uid);
            });
            QObject::connect(reply, &KNotificationAction::activated, [&dataStore, account, folder, uid]() {
                emit dataStore.notificationReplyRequested(account, folder, uid);
            });

            n->sendEvent();
        });
    // Enable notifications only after the initial full sync completes.
    // Individual folder syncs during the initial bulk import should not
    // trigger desktop notifications — that's too spammy.
    {
        // Enable desktop notifications after every account has completed
        // at least one sync cycle.  This prevents a flood of notifications
        // for pre-existing messages during the initial bulk sync.
        // Desktop notifications are on by default. Disable them when a new
        // account is added (to suppress the flood from initial bulk sync),
        // then re-enable once the sync settles.
        // Start disabled — enable after the first sync cycle completes.
        dataStore.setDesktopNotifyEnabled(false);

        QObject::connect(&accountManager, &AccountManager::accountsChanged, &dataStore,
            [&dataStore]() {
                dataStore.setDesktopNotifyEnabled(false);
            });
        QObject::connect(&accountManager, &AccountManager::anySyncingChanged, &dataStore,
            [&dataStore, &accountManager]() {
                if (!accountManager.anySyncing())
                    dataStore.setDesktopNotifyEnabled(true);
            });
    }

    CalendarLayoutHelper calendarLayoutHelper(&engine);

    engine.rootContext()->setContextProperty("calendarLayoutHelper", &calendarLayoutHelper);
    engine.rootContext()->setContextProperty("providerProfiles", &providerProfiles);
    engine.rootContext()->setContextProperty("accountSetup", &accountSetup);
    engine.rootContext()->setContextProperty("accountRepository", &accountRepository);
    engine.rootContext()->setContextProperty("dataStore", &dataStore);
    engine.rootContext()->setContextProperty("messageListModel", &messageListModel);
    engine.rootContext()->setContextProperty("htmlProcessor", &htmlProcessor);
    engine.rootContext()->setContextProperty("imapService", &imapService);
    engine.rootContext()->setContextProperty("googleApiService", &googleApiService);
    engine.rootContext()->setContextProperty("smtpService", &smtpService);
    engine.rootContext()->setContextProperty("pgpKeyManager", &pgpKeyManager);
    engine.rootContext()->setContextProperty("accountManager", &accountManager);

    // Find a condensed sans-serif font, preferring specific families.
    {
        const QStringList preferred = {
            "Open Sans Condensed"_L1,
            "DejaVu Sans Condensed"_L1,
            "Roboto Condensed"_L1,
            "Fira Sans Condensed"_L1,
        };
        const auto allFamilies = QFontDatabase::families();
        const QSet<QString> installed(allFamilies.cbegin(), allFamilies.cend());
        QString condensed;
        for (const auto &pref : preferred) {
            if (installed.contains(pref)) { condensed = pref; break; }
        }
        // Fallback: any installed condensed sans font.
        if (condensed.isEmpty()) {
            for (const auto &f : allFamilies) {
                if (f.endsWith("Condensed"_L1, Qt::CaseInsensitive)
                    && !QFontDatabase::isFixedPitch(f)) {
                    condensed = f;
                    break;
                }
            }
        }
        engine.rootContext()->setContextProperty("condensedFontFamily",
            condensed.isEmpty() ? QFontInfo(QFont()).family() : condensed);
    }
#if defined(NDEBUG)
    engine.rootContext()->setContextProperty("kestrelDebugBuild", false);
#else
    engine.rootContext()->setContextProperty("kestrelDebugBuild", true);
#endif

    const QUrl url("qrc:/qml/Main.qml"_L1);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [&splash]() {
            splash.reset();
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);

    QWindow *mainWindow = nullptr;
    if (!engine.rootObjects().isEmpty()) {
        mainWindow = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    }

    if (splash) {
        if (mainWindow) {
            mainWindow->setVisibility(QWindow::Hidden);
            app.processEvents();
        }

        splash->execUntilReady(app, imapService);
        splash.reset();

        if (mainWindow) {
            mainWindow->show();
            mainWindow->setVisibility(QWindow::Windowed);
            mainWindow->raise();
            mainWindow->requestActivate();
        }
    }

    // Expose-event watcher: emits windowReExposed() when the Wayland compositor
    // shows the surface again after hiding it (desktop/activity switch, minimize).
    WindowExposeWatcher *exposeWatcher = nullptr;
    if (mainWindow) {
        exposeWatcher = new WindowExposeWatcher(mainWindow, &app);
        engine.rootContext()->setContextProperty("windowExposeWatcher"_L1, exposeWatcher);
    }

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *trayMenu = new QMenu();
        auto *showHideAction = trayMenu->addAction("Show / Hide"_L1);
        trayMenu->addSeparator();
        auto *quitAction = trayMenu->addAction("Quit"_L1);

        auto *trayIcon = new QSystemTrayIcon(&app);
        const auto tray = kestrelIcon;
        trayIcon->setIcon(tray);
        trayIcon->setToolTip("Kestrel Mail"_L1);
        trayIcon->setContextMenu(trayMenu);

        auto toggleMainWindow = [&engine]() {
            if (engine.rootObjects().isEmpty()) return;
            auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
            if (!window) return;

            if (window->isVisible() && window->visibility() != QWindow::Minimized) {
                window->hide();
            } else {
                window->show();
                window->setVisibility(QWindow::Windowed);
                window->raise();
                window->requestActivate();
            }
        };

        QObject::connect(showHideAction, &QAction::triggered, &app, toggleMainWindow);
        QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);
        QObject::connect(trayIcon, &QSystemTrayIcon::activated, &app,
                         [toggleMainWindow](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                                 toggleMainWindow();
                             }
                         });

        trayIcon->show();
    }

    return app.exec();
}

#include "main.moc"
