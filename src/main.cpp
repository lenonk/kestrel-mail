#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QWindow>
#include <QSplashScreen>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QFont>
#include <QPainter>
#include <QThread>
#include <QRandomGenerator>
#include <QEvent>
#include <cmath>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

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

QSplashScreen *splash = nullptr;
    QElapsedTimer splashTimer;
    {
        QPixmap splashPixmap(QStringLiteral(":/data/assets/splash.png"));
        if (!splashPixmap.isNull()) {
            splash = new QSplashScreen(splashPixmap);
            splash->setWindowFlag(Qt::WindowStaysOnTopHint, true);
            QFont splashMsgFont = splash->font();
            splashMsgFont.setPointSize(12);
            splashMsgFont.setBold(true);
            splash->setFont(splashMsgFont);
            splash->show();
            splashTimer.start();
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
    MessageListModel messageListModel(&engine);
    messageListModel.setDataStore(&dataStore);
    HtmlProcessor htmlProcessor(&engine);
    ImapService imapService(&accountRepository, &dataStore, &tokenVault, &engine);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &imapService, &ImapService::shutdown);
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
        // Keep app window hidden until splash finishes.
        if (mainWindow) {
            mainWindow->setVisibility(QWindow::Hidden);
            app.processEvents();
        }

        // static constexpr qint64 kSplashMinMs = 10000;
        static constexpr qint64 kSplashMinMs = 10000;

        const QPixmap baseSplash(QStringLiteral(":/data/assets/splash.png"));
        int frame = 0;
        static constexpr qint64 kSpinnerFrameMs = 75;
        qint64 lastFrameMs = -kSpinnerFrameMs;

        static const QStringList statusLines = {
            QStringLiteral("Warming up tiny mail hawks…"), QStringLiteral("Syncing bird thoughts…"),
            QStringLiteral("Untangling inbox vines…"), QStringLiteral("Teaching owls to sort receipts…"),
            QStringLiteral("Locating suspicious newsletters…"), QStringLiteral("Polishing unread counters…"),
            QStringLiteral("Rehydrating forgotten threads…"), QStringLiteral("Sharpening reply claws…"),
            QStringLiteral("Dusting off archived chaos…"), QStringLiteral("Calibrating smug filters…"),
            QStringLiteral("Aligning stars and sender names…"), QStringLiteral("Fluffing mailbox feathers…"),
            QStringLiteral("Negotiating with Promotions tab…"), QStringLiteral("Gently poking IMAP…"),
            QStringLiteral("Bribing spam goblins…"), QStringLiteral("Whispering to sync daemons…"),
            QStringLiteral("Counting unread anxieties…"), QStringLiteral("Folding notifications neatly…"),
            QStringLiteral("Aerating stale email threads…"), QStringLiteral("Detangling category spaghetti…"),
            QStringLiteral("Repainting tiny envelope icons…"), QStringLiteral("Finding where that one email went…"),
            QStringLiteral("Loading strategic sass…"), QStringLiteral("Calming the junk vortex…"),
            QStringLiteral("Summoning responsible productivity…"), QStringLiteral("Auditing subject line nonsense…"),
            QStringLiteral("Inflating courage for reply-all…"), QStringLiteral("Sharpening search instincts…"),
            QStringLiteral("Refilling coffee for workers…"), QStringLiteral("Untwisting quoted replies…"),
            QStringLiteral("Locating attachments in the void…"), QStringLiteral("Tuning folder gravity…"),
            QStringLiteral("Buffing thread dedupe routines…"), QStringLiteral("Decrypting calendar vibes…"),
            QStringLiteral("Politely ignoring tracking pixels…"), QStringLiteral("Restoring inbox equilibrium…"),
            QStringLiteral("Counting very important pigeons…"), QStringLiteral("Untangling signature markdown…"),
            QStringLiteral("Waking cautious automations…"), QStringLiteral("Flipping unread bits with flair…"),
            QStringLiteral("Training filters to behave…"), QStringLiteral("Cross-checking sent folder lore…"),
            QStringLiteral("Loading socially acceptable confidence…"), QStringLiteral("Decoding cryptic invites…"),
            QStringLiteral("Polishing Important chevrons…"), QStringLiteral("Refreshing message plumage…"),
            QStringLiteral("Staring firmly at race conditions…"), QStringLiteral("Sweeping draft-folder cobwebs…"),
            QStringLiteral("Applying tasteful chaos…"), QStringLiteral("Reassuring fragile SQLite feelings…"),
            QStringLiteral("Measuring sync optimism…"), QStringLiteral("Converting panic into pagination…"),
            QStringLiteral("Pretending this is under control…"), QStringLiteral("Gathering tiny facts quickly…"),
            QStringLiteral("Smoothing jagged inbox edges…"), QStringLiteral("Massaging folder hierarchies…"),
            QStringLiteral("Unhiding deeply buried context…"), QStringLiteral("Verifying that mail was definitely sent…"),
            QStringLiteral("Brushing lint off message IDs…"), QStringLiteral("Translating corporate urgency…"),
            QStringLiteral("Inspecting suspiciously cheerful updates…"), QStringLiteral("Disarming newsletter ambushes…"),
            QStringLiteral("Hunting duplicate thread ghosts…"), QStringLiteral("Hydrating message bodies…"),
            QStringLiteral("Reconciling folder reality…"), QStringLiteral("Finding peace in UID space…"),
            QStringLiteral("Threading needles through haystacks…"), QStringLiteral("Optimizing dramatic pauses…"),
            QStringLiteral("Assembling seriousness from spare parts…"), QStringLiteral("Re-centering Inbox chakra…"),
            QStringLiteral("Reindexing chaos with confidence…"), QStringLiteral("Tickling stale caches awake…"),
            QStringLiteral("Negotiating with SMTP politely…"), QStringLiteral("Preventing accidental nonsense…"),
            QStringLiteral("Rounding up rogue labels…"), QStringLiteral("Applying anti-doomscroll varnish…"),
            QStringLiteral("Inflating thread previews…"), QStringLiteral("Teaching Trash to let go…"),
            QStringLiteral("Reconciling all things All Mail…"), QStringLiteral("Stabilizing specific edge cases…"),
            QStringLiteral("Carefully poking background sync…"), QStringLiteral("Sweeping search index crumbs…"),
            QStringLiteral("Asking IMAP nicely, again…"), QStringLiteral("Transmuting backlog into progress…"),
            QStringLiteral("Compiling tiny acts of competence…"), QStringLiteral("Finessing folder indentation drama…"),
            QStringLiteral("Polishing pre-alpha audacity…"), QStringLiteral("Preparing for unread mail…"),
            QStringLiteral("Inspecting feral notifications…"), QStringLiteral("Smoothing avatar edge pixels…"),
            QStringLiteral("Plotting inbox maneuvers…"), QStringLiteral("Converting TODOs into done-ish…"),
            QStringLiteral("Training categorization gremlins…"), QStringLiteral("Dodging flaky network weather…"),
            QStringLiteral("Mapping tiny constellations of mail…"), QStringLiteral("Balancing urgency and calm…"),
            QStringLiteral("Deploying miniature mail falcons…"), QStringLiteral("Herding message metadata…"),
            QStringLiteral("Reinforcing anti-chaos scaffolding…"), QStringLiteral("Making pre-alpha look intentional…"),
            QStringLiteral("Summoning one more clean sync…")
        };

        QString currentStatus = statusLines.at(QRandomGenerator::global()->bounded(statusLines.size()));
        qint64 nextStatusChangeMs = QRandomGenerator::global()->bounded(1000, 3001);

        while (splashTimer.isValid() && splashTimer.elapsed() < kSplashMinMs) {
            const qint64 nowMs = splashTimer.elapsed();
            if (nowMs >= nextStatusChangeMs) {
                currentStatus = statusLines.at(QRandomGenerator::global()->bounded(statusLines.size()));
                nextStatusChangeMs = nowMs + QRandomGenerator::global()->bounded(1000, 3001);
            }

            if (nowMs - lastFrameMs >= kSpinnerFrameMs) {
                QPixmap framePix = baseSplash;
                QPainter p(&framePix);
                p.setRenderHint(QPainter::Antialiasing, true);

                const int cx = framePix.width() / 2;
                const int cy = 418;
                const int radius = 23;
                const int dotCount = 9;

                for (int i = 0; i < dotCount; ++i) {
                    constexpr double kTwoPi = 6.28318530717958647692;
                    const double a = (kTwoPi * i) / dotCount;
                    const int x = cx + static_cast<int>(std::cos(a) * radius);
                    const int y = cy + static_cast<int>(std::sin(a) * radius);

                    const int frameIdx = frame % dotCount;
                    const int age = (i - frameIdx + dotCount) % dotCount;
                    const int alpha = qBound(40, 255 - age * 20, 255);

                    // Egg spinner: tiny eggs orbiting
                    QColor shell(QStringLiteral("#c58bff"));
                    shell.setAlpha(alpha);
                    QColor yolk(QStringLiteral("#ffd54a"));
                    yolk.setAlpha(qBound(120, alpha, 255));

                    p.save();
                    p.translate(x, y);
                    p.rotate((a * 180.0 / 3.14159265358979323846) + 90.0);
                    p.setPen(Qt::NoPen);
                    p.setBrush(shell);
                    p.drawEllipse(QRectF(-5.0, -7.5, 10.0, 15.0));
                    p.setBrush(yolk);
                    p.drawEllipse(QRectF(-2.5, 0.0, 5.0, 5.0));
                    p.restore();
                }

                QFont f = p.font();
                f.setPointSize(12);
                f.setBold(true);
                p.setFont(f);
                p.setPen(QColor(QStringLiteral("#498CFD")));
                p.drawText(QRect(12, cy + 42, framePix.width() - 24, 58),
                           Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                           currentStatus);

                p.end();
                splash->setPixmap(framePix);
                frame++;
                lastFrameMs = nowMs;
            }

            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(25);
        }

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
