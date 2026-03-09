#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QWindow>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QByteArray>
#include <QDebug>
#include <cstdlib>

#if defined(__linux__)
#include <execinfo.h>
#endif

#include "core/accounts/accountrepository.h"
#include "core/accounts/accountsetupcontroller.h"
#include "core/accounts/providerprofileservice.h"
#include "core/auth/filetokenvault.h"
#include "core/auth/oauthservice.h"
#include "core/store/datastore.h"
#include "core/store/messagelistmodel.h"
#include "core/transport/imap/imapservice.h"

static QtMessageHandler g_prevMsgHandler = nullptr;

static void kestrelMsgHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
#if defined(__linux__)
    if (msg.contains("QFont::setPixelSize: Pixel size <= 0", Qt::CaseInsensitive)) {
        void *stack[64];
        const int n = backtrace(stack, 64);
        qWarning().noquote() << "[qfont-trace] captured stack frames=" << n;
        if (n > 0) {
            char **symbols = backtrace_symbols(stack, n);
            if (symbols) {
                for (int i = 0; i < n; ++i)
                    qWarning().noquote() << "[qfont-trace]" << symbols[i];
                free(symbols);
            }
        }
    }
#else
    Q_UNUSED(ctx)
#endif

    if (g_prevMsgHandler)
        g_prevMsgHandler(type, ctx, msg);
}

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

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("KESTREL_TRACE_QFONT_WARN"))
        g_prevMsgHandler = qInstallMessageHandler(kestrelMsgHandler);

    QtWebEngineQuick::initialize();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kestrel-mail"));
    app.setOrganizationDomain(QStringLiteral("github.com"));
    app.setOrganizationName(QStringLiteral("lenonk"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("mail-message-new")));

    KLocalizedString::setApplicationDomain("kestrel-mail");

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
    MessageListModel messageListModel(&engine);
    messageListModel.setDataStore(&dataStore);
    ImapService imapService(&accountRepository, &dataStore, &tokenVault, &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &imapService, &ImapService::shutdown);

    engine.rootContext()->setContextProperty("providerProfiles", &providerProfiles);
    engine.rootContext()->setContextProperty("accountSetup", &accountSetup);
    engine.rootContext()->setContextProperty("accountRepository", &accountRepository);
    engine.rootContext()->setContextProperty("dataStore", &dataStore);
    engine.rootContext()->setContextProperty("messageListModel", &messageListModel);
    engine.rootContext()->setContextProperty("imapService", &imapService);
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
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.load(url);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        auto *trayMenu = new QMenu();
        auto *showHideAction = trayMenu->addAction(QStringLiteral("Show / Hide"));
        trayMenu->addSeparator();
        auto *quitAction = trayMenu->addAction(QStringLiteral("Quit"));

        auto *trayIcon = new QSystemTrayIcon(&app);
        const QIcon tray = QIcon::fromTheme(QStringLiteral("mail-message-new"), app.windowIcon());
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
