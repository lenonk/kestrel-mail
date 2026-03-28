#include "splashscreen.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QRandomGenerator>
#include <QThread>
#include <cmath>

#include "src/core/transport/imap/imapservice.h"

static constexpr qint64 kSpinnerFrameMs = 75;
static constexpr qint64 kSplashTimeoutMs = 15000;
static constexpr qint64 kSplashMinMs = 1500;

SplashScreen::SplashScreen(const QPixmap &pixmap)
    : QSplashScreen(pixmap)
    , m_basePixmap(pixmap)
{
    setWindowFlag(Qt::WindowStaysOnTopHint, true);

    QFont f = font();
    f.setPointSize(12);
    f.setBold(true);
    setFont(f);

    m_currentStatus = statusLines().at(
        QRandomGenerator::global()->bounded(statusLines().size()));
    m_nextStatusChangeMs = QRandomGenerator::global()->bounded(1000, 3001);

    m_timer.start();
}

void
SplashScreen::execUntilReady(QApplication &app, ImapService &imap)
{
    const int expectedConns = imap.expectedPoolConnections();

    while (m_timer.isValid()) {
        const qint64 elapsed = m_timer.elapsed();
        const int readyConns = imap.poolConnectionsReady();

        // Hard timeout — never hang forever.
        if (elapsed >= kSplashTimeoutMs)
            break;
        // No accounts / offline — show splash briefly then exit.
        if (expectedConns == 0 && elapsed >= kSplashMinMs)
            break;
        // All connections ready (and minimum display time met).
        if (expectedConns > 0 && readyConns >= expectedConns && elapsed >= kSplashMinMs)
            break;

        const qint64 nowMs = elapsed;

        // Cycle witty status lines every 1–3 seconds.
        if (nowMs >= m_nextStatusChangeMs) {
            m_currentStatus = statusLines().at(
                QRandomGenerator::global()->bounded(statusLines().size()));
            m_nextStatusChangeMs = nowMs + QRandomGenerator::global()->bounded(1000, 3001);
        }

        // Render spinner frame at ~75 ms intervals.
        if (nowMs - m_lastFrameMs >= kSpinnerFrameMs) {
            renderFrame(m_basePixmap, m_currentStatus);
            m_frame++;
            m_lastFrameMs = nowMs;
        }

        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
}

void
SplashScreen::renderFrame(const QPixmap &base, const QString &status)
{
    QPixmap framePix = base;
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

        const int frameIdx = m_frame % dotCount;
        const int age = (i - frameIdx + dotCount) % dotCount;
        const int alpha = qBound(40, 255 - age * 20, 255);

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
               status);

    p.end();
    setPixmap(framePix);
}

const QStringList &
SplashScreen::statusLines()
{
    static const QStringList lines = {
        QStringLiteral("Warming up tiny mail hawks\u2026"), QStringLiteral("Syncing bird thoughts\u2026"),
        QStringLiteral("Untangling inbox vines\u2026"), QStringLiteral("Teaching owls to sort receipts\u2026"),
        QStringLiteral("Locating suspicious newsletters\u2026"), QStringLiteral("Polishing unread counters\u2026"),
        QStringLiteral("Rehydrating forgotten threads\u2026"), QStringLiteral("Sharpening reply claws\u2026"),
        QStringLiteral("Dusting off archived chaos\u2026"), QStringLiteral("Calibrating smug filters\u2026"),
        QStringLiteral("Aligning stars and sender names\u2026"), QStringLiteral("Fluffing mailbox feathers\u2026"),
        QStringLiteral("Negotiating with Promotions tab\u2026"), QStringLiteral("Gently poking IMAP\u2026"),
        QStringLiteral("Bribing spam goblins\u2026"), QStringLiteral("Whispering to sync daemons\u2026"),
        QStringLiteral("Counting unread anxieties\u2026"), QStringLiteral("Folding notifications neatly\u2026"),
        QStringLiteral("Aerating stale email threads\u2026"), QStringLiteral("Detangling category spaghetti\u2026"),
        QStringLiteral("Repainting tiny envelope icons\u2026"), QStringLiteral("Finding where that one email went\u2026"),
        QStringLiteral("Loading strategic sass\u2026"), QStringLiteral("Calming the junk vortex\u2026"),
        QStringLiteral("Summoning responsible productivity\u2026"), QStringLiteral("Auditing subject line nonsense\u2026"),
        QStringLiteral("Inflating courage for reply-all\u2026"), QStringLiteral("Sharpening search instincts\u2026"),
        QStringLiteral("Refilling coffee for workers\u2026"), QStringLiteral("Untwisting quoted replies\u2026"),
        QStringLiteral("Locating attachments in the void\u2026"), QStringLiteral("Tuning folder gravity\u2026"),
        QStringLiteral("Buffing thread dedupe routines\u2026"), QStringLiteral("Decrypting calendar vibes\u2026"),
        QStringLiteral("Politely ignoring tracking pixels\u2026"), QStringLiteral("Restoring inbox equilibrium\u2026"),
        QStringLiteral("Counting very important pigeons\u2026"), QStringLiteral("Untangling signature markdown\u2026"),
        QStringLiteral("Waking cautious automations\u2026"), QStringLiteral("Flipping unread bits with flair\u2026"),
        QStringLiteral("Training filters to behave\u2026"), QStringLiteral("Cross-checking sent folder lore\u2026"),
        QStringLiteral("Loading socially acceptable confidence\u2026"), QStringLiteral("Decoding cryptic invites\u2026"),
        QStringLiteral("Polishing Important chevrons\u2026"), QStringLiteral("Refreshing message plumage\u2026"),
        QStringLiteral("Staring firmly at race conditions\u2026"), QStringLiteral("Sweeping draft-folder cobwebs\u2026"),
        QStringLiteral("Applying tasteful chaos\u2026"), QStringLiteral("Reassuring fragile SQLite feelings\u2026"),
        QStringLiteral("Measuring sync optimism\u2026"), QStringLiteral("Converting panic into pagination\u2026"),
        QStringLiteral("Pretending this is under control\u2026"), QStringLiteral("Gathering tiny facts quickly\u2026"),
        QStringLiteral("Smoothing jagged inbox edges\u2026"), QStringLiteral("Massaging folder hierarchies\u2026"),
        QStringLiteral("Unhiding deeply buried context\u2026"), QStringLiteral("Verifying that mail was definitely sent\u2026"),
        QStringLiteral("Brushing lint off message IDs\u2026"), QStringLiteral("Translating corporate urgency\u2026"),
        QStringLiteral("Inspecting suspiciously cheerful updates\u2026"), QStringLiteral("Disarming newsletter ambushes\u2026"),
        QStringLiteral("Hunting duplicate thread ghosts\u2026"), QStringLiteral("Hydrating message bodies\u2026"),
        QStringLiteral("Reconciling folder reality\u2026"), QStringLiteral("Finding peace in UID space\u2026"),
        QStringLiteral("Threading needles through haystacks\u2026"), QStringLiteral("Optimizing dramatic pauses\u2026"),
        QStringLiteral("Assembling seriousness from spare parts\u2026"), QStringLiteral("Re-centering Inbox chakra\u2026"),
        QStringLiteral("Reindexing chaos with confidence\u2026"), QStringLiteral("Tickling stale caches awake\u2026"),
        QStringLiteral("Negotiating with SMTP politely\u2026"), QStringLiteral("Preventing accidental nonsense\u2026"),
        QStringLiteral("Rounding up rogue labels\u2026"), QStringLiteral("Applying anti-doomscroll varnish\u2026"),
        QStringLiteral("Inflating thread previews\u2026"), QStringLiteral("Teaching Trash to let go\u2026"),
        QStringLiteral("Reconciling all things All Mail\u2026"), QStringLiteral("Stabilizing specific edge cases\u2026"),
        QStringLiteral("Carefully poking background sync\u2026"), QStringLiteral("Sweeping search index crumbs\u2026"),
        QStringLiteral("Asking IMAP nicely, again\u2026"), QStringLiteral("Transmuting backlog into progress\u2026"),
        QStringLiteral("Compiling tiny acts of competence\u2026"), QStringLiteral("Finessing folder indentation drama\u2026"),
        QStringLiteral("Polishing pre-alpha audacity\u2026"), QStringLiteral("Preparing for unread mail\u2026"),
        QStringLiteral("Inspecting feral notifications\u2026"), QStringLiteral("Smoothing avatar edge pixels\u2026"),
        QStringLiteral("Plotting inbox maneuvers\u2026"), QStringLiteral("Converting TODOs into done-ish\u2026"),
        QStringLiteral("Training categorization gremlins\u2026"), QStringLiteral("Dodging flaky network weather\u2026"),
        QStringLiteral("Mapping tiny constellations of mail\u2026"), QStringLiteral("Balancing urgency and calm\u2026"),
        QStringLiteral("Deploying miniature mail falcons\u2026"), QStringLiteral("Herding message metadata\u2026"),
        QStringLiteral("Reinforcing anti-chaos scaffolding\u2026"), QStringLiteral("Making pre-alpha look intentional\u2026"),
        QStringLiteral("Summoning one more clean sync\u2026")
    };
    return lines;
}
