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
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "ui/splashscreen.h"
#include "core/htmlprocessor.h"
#include "core/accounts/accountrepository.h"
#include "core/accounts/accountsetupcontroller.h"
#include "core/accounts/providerprofileservice.h"
#include "core/auth/filetokenvault.h"
#include "core/auth/oauthservice.h"
#include "core/store/datastore.h"
#include "core/store/messagelistmodel.h"
#include "core/transport/imap/imapservice.h"
#include "core/transport/smtp/smtpservice.h"

class CachingNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new QNetworkAccessManager(parent);
        auto *diskCache = new QNetworkDiskCache(nam);

        const QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString cachePath = cacheBase + QStringLiteral("/http");
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
    app.setApplicationName(QStringLiteral("kestrel-mail"));
    app.setOrganizationDomain(QStringLiteral("github.com"));
    app.setOrganizationName(QStringLiteral("lenonk"));
    const QIcon kestrelIcon(QStringLiteral(":/data/assets/kestrel-mail.png"));
    app.setWindowIcon(kestrelIcon.isNull()
                          ? QIcon::fromTheme(QStringLiteral("mail-message-new"))
                          : kestrelIcon);

    KLocalizedString::setApplicationDomain("kestrel-mail");

    SplashScreen *splash = nullptr;
    {
        QPixmap splashPixmap(QStringLiteral(":/data/assets/splash.png"));
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
    FileTokenVault tokenVault; // TODO: replace with KWallet-backed vault on KDE runtime.
    OAuthService oauthService(&tokenVault, &engine);
    AccountSetupController accountSetup(&providerProfiles, &oauthService, &accountRepository, &engine);
    DataStore dataStore(&engine);
    dataStore.init();
    dataStore.quickCheck();
    MessageListModel messageListModel(&engine);
    messageListModel.setDataStore(&dataStore);
    HtmlProcessor htmlProcessor(&engine);
    ImapService imapService(&accountRepository, &dataStore, &tokenVault, &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &imapService, &ImapService::shutdown);
    imapService.initializeConnectionPool();
    SmtpService smtpService(&accountRepository, &tokenVault, &engine);

    engine.rootContext()->setContextProperty("providerProfiles", &providerProfiles);
    engine.rootContext()->setContextProperty("accountSetup", &accountSetup);
    engine.rootContext()->setContextProperty("accountRepository", &accountRepository);
    engine.rootContext()->setContextProperty("dataStore", &dataStore);
    engine.rootContext()->setContextProperty("messageListModel", &messageListModel);
    engine.rootContext()->setContextProperty("htmlProcessor", &htmlProcessor);
    engine.rootContext()->setContextProperty("imapService", &imapService);
    engine.rootContext()->setContextProperty("smtpService", &smtpService);
#if defined(NDEBUG)
    engine.rootContext()->setContextProperty("kestrelDebugBuild", false);
#else
    engine.rootContext()->setContextProperty("kestrelDebugBuild", true);
#endif

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
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
        engine.rootContext()->setContextProperty(QStringLiteral("windowExposeWatcher"), exposeWatcher);
    }

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *trayMenu = new QMenu();
        auto *showHideAction = trayMenu->addAction(QStringLiteral("Show / Hide"));
        trayMenu->addSeparator();
        auto *quitAction = trayMenu->addAction(QStringLiteral("Quit"));

        auto *trayIcon = new QSystemTrayIcon(&app);
        // const QIcon tray = QIcon::fromTheme(QStringLiteral("mail-message-new"), app.windowIcon());
        const QIcon tray = kestrelIcon;
        trayIcon->setIcon(tray);
        trayIcon->setToolTip(QStringLiteral("Kestrel Mail"));
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
