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
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "ui/splashscreen.h"
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
#include "core/transport/smtp/smtpservice.h"
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

    SplashScreen *splash = nullptr;
    {
        QPixmap splashPixmap(":/data/assets/splash.png"_L1);
        if (!splashPixmap.isNull()) {
            splash = new SplashScreen(splashPixmap);
            splash->show();
            app.processEvents();
        }
    }

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new CachingNetworkAccessManagerFactory());
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));

    ProviderProfileService providerProfiles(&engine);
    AccountRepository accountRepository(&engine);
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
    AccountSetupController accountSetup(&providerProfiles, &oauthService, &accountRepository, &engine);
    DataStore dataStore(&engine);
    dataStore.init();
    dataStore.quickCheck();
    MessageListModel messageListModel(&engine);
    messageListModel.setDataStore(&dataStore);
    HtmlProcessor htmlProcessor(&engine);
    ImapService imapService(&accountRepository, &dataStore, tokenVault.get(), &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &imapService, &ImapService::shutdown);
    imapService.initializeConnectionPool();
    SmtpService smtpService(&accountRepository, tokenVault.get(), &engine);

    // Desktop notification for new mail.
    QObject::connect(&dataStore, &DataStore::newMailReceived, &app,
        [&dataStore, &imapService](const QVariantMap &info) {
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
            n->setText(body);

            // Load sender avatar or generate initials fallback.
            const QString avatarUrl = dataStore.avatarForEmail(email);
            QPixmap avatar;
            if (!avatarUrl.isEmpty()) {
                QString path = avatarUrl;
                if (path.startsWith("file://"_L1))
                    path = path.mid(7);
                avatar = QPixmap(path);
                if (!avatar.isNull())
                    avatar = avatar.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
    // Enable notifications after the first successful sync so a cold-start
    // bulk import doesn't spam the desktop. On warm starts (DB already has
    // messages), enable immediately so the very first IDLE message is notified.
    if (dataStore.inboxCount() > 0) {
        dataStore.setDesktopNotifyEnabled(true);
    } else {
        auto enableNotify = [&dataStore](bool ok, const QString &) {
            if (ok && !dataStore.desktopNotifyEnabled())
                dataStore.setDesktopNotifyEnabled(true);
        };
        QObject::connect(&imapService, &ImapService::syncFinished, &dataStore, enableNotify);
        QObject::connect(&imapService, &ImapService::realtimeStatus, &dataStore, enableNotify);
    }

    engine.rootContext()->setContextProperty("providerProfiles", &providerProfiles);
    engine.rootContext()->setContextProperty("accountSetup", &accountSetup);
    engine.rootContext()->setContextProperty("accountRepository", &accountRepository);
    engine.rootContext()->setContextProperty("dataStore", &dataStore);
    engine.rootContext()->setContextProperty("messageListModel", &messageListModel);
    engine.rootContext()->setContextProperty("htmlProcessor", &htmlProcessor);
    engine.rootContext()->setContextProperty("imapService", &imapService);
    engine.rootContext()->setContextProperty("smtpService", &smtpService);

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
        [splash]() {
            if (splash) {
                splash->close();
                splash->deleteLater();
            }
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
        splash->close();
        splash->deleteLater();

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
        // const QIcon tray = QIcon::fromTheme("mail-message-new"_L1, app.windowIcon());
        const QIcon tray = kestrelIcon;
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
