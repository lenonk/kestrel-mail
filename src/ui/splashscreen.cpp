#include "splashscreen.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QRandomGenerator>
#include <QThread>
#include <cmath>

#include "src/core/transport/imap/imapservice.h"

using namespace Qt::Literals::StringLiterals;

static constexpr qint64 kSpinnerFrameMs = 75;
static constexpr qint64 kSplashTimeoutMs = 15000;
static constexpr qint64 kSplashMinMs = 3000;

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

        QColor shell("#c58bff"_L1);
        shell.setAlpha(alpha);
        QColor yolk("#ffd54a"_L1);
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
    p.setPen(QColor("#498CFD"_L1));
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
        u"Warming up tiny mail hawks\u2026"_s, u"Syncing bird thoughts\u2026"_s,
        u"Untangling inbox vines\u2026"_s, u"Teaching owls to sort receipts\u2026"_s,
        u"Locating suspicious newsletters\u2026"_s, u"Polishing unread counters\u2026"_s,
        u"Rehydrating forgotten threads\u2026"_s, u"Sharpening reply claws\u2026"_s,
        u"Dusting off archived chaos\u2026"_s, u"Calibrating smug filters\u2026"_s,
        u"Aligning stars and sender names\u2026"_s, u"Fluffing mailbox feathers\u2026"_s,
        u"Negotiating with Promotions tab\u2026"_s, u"Gently poking IMAP\u2026"_s,
        u"Bribing spam goblins\u2026"_s, u"Whispering to sync daemons\u2026"_s,
        u"Counting unread anxieties\u2026"_s, u"Folding notifications neatly\u2026"_s,
        u"Aerating stale email threads\u2026"_s, u"Detangling category spaghetti\u2026"_s,
        u"Repainting tiny envelope icons\u2026"_s, u"Finding where that one email went\u2026"_s,
        u"Loading strategic sass\u2026"_s, u"Calming the junk vortex\u2026"_s,
        u"Summoning responsible productivity\u2026"_s, u"Auditing subject line nonsense\u2026"_s,
        u"Inflating courage for reply-all\u2026"_s, u"Sharpening search instincts\u2026"_s,
        u"Refilling coffee for workers\u2026"_s, u"Untwisting quoted replies\u2026"_s,
        u"Locating attachments in the void\u2026"_s, u"Tuning folder gravity\u2026"_s,
        u"Buffing thread dedupe routines\u2026"_s, u"Decrypting calendar vibes\u2026"_s,
        u"Politely ignoring tracking pixels\u2026"_s, u"Restoring inbox equilibrium\u2026"_s,
        u"Counting very important pigeons\u2026"_s, u"Untangling signature markdown\u2026"_s,
        u"Waking cautious automations\u2026"_s, u"Flipping unread bits with flair\u2026"_s,
        u"Training filters to behave\u2026"_s, u"Cross-checking sent folder lore\u2026"_s,
        u"Loading socially acceptable confidence\u2026"_s, u"Decoding cryptic invites\u2026"_s,
        u"Polishing Important chevrons\u2026"_s, u"Refreshing message plumage\u2026"_s,
        u"Staring firmly at race conditions\u2026"_s, u"Sweeping draft-folder cobwebs\u2026"_s,
        u"Applying tasteful chaos\u2026"_s, u"Reassuring fragile SQLite feelings\u2026"_s,
        u"Measuring sync optimism\u2026"_s, u"Converting panic into pagination\u2026"_s,
        u"Pretending this is under control\u2026"_s, u"Gathering tiny facts quickly\u2026"_s,
        u"Smoothing jagged inbox edges\u2026"_s, u"Massaging folder hierarchies\u2026"_s,
        u"Unhiding deeply buried context\u2026"_s, u"Verifying that mail was definitely sent\u2026"_s,
        u"Brushing lint off message IDs\u2026"_s, u"Translating corporate urgency\u2026"_s,
        u"Inspecting suspiciously cheerful updates\u2026"_s, u"Disarming newsletter ambushes\u2026"_s,
        u"Hunting duplicate thread ghosts\u2026"_s, u"Hydrating message bodies\u2026"_s,
        u"Reconciling folder reality\u2026"_s, u"Finding peace in UID space\u2026"_s,
        u"Threading needles through haystacks\u2026"_s, u"Optimizing dramatic pauses\u2026"_s,
        u"Assembling seriousness from spare parts\u2026"_s, u"Re-centering Inbox chakra\u2026"_s,
        u"Reindexing chaos with confidence\u2026"_s, u"Tickling stale caches awake\u2026"_s,
        u"Negotiating with SMTP politely\u2026"_s, u"Preventing accidental nonsense\u2026"_s,
        u"Rounding up rogue labels\u2026"_s, u"Applying anti-doomscroll varnish\u2026"_s,
        u"Inflating thread previews\u2026"_s, u"Teaching Trash to let go\u2026"_s,
        u"Reconciling all things All Mail\u2026"_s, u"Stabilizing specific edge cases\u2026"_s,
        u"Carefully poking background sync\u2026"_s, u"Sweeping search index crumbs\u2026"_s,
        u"Asking IMAP nicely, again\u2026"_s, u"Transmuting backlog into progress\u2026"_s,
        u"Compiling tiny acts of competence\u2026"_s, u"Finessing folder indentation drama\u2026"_s,
        u"Polishing pre-alpha audacity\u2026"_s, u"Preparing for unread mail\u2026"_s,
        u"Inspecting feral notifications\u2026"_s, u"Smoothing avatar edge pixels\u2026"_s,
        u"Plotting inbox maneuvers\u2026"_s, u"Converting TODOs into done-ish\u2026"_s,
        u"Training categorization gremlins\u2026"_s, u"Dodging flaky network weather\u2026"_s,
        u"Mapping tiny constellations of mail\u2026"_s, u"Balancing urgency and calm\u2026"_s,
        u"Deploying miniature mail falcons\u2026"_s, u"Herding message metadata\u2026"_s,
        u"Reinforcing anti-chaos scaffolding\u2026"_s, u"Making pre-alpha look intentional\u2026"_s,
        u"Summoning one more clean sync\u2026"_s
    };
    return lines;
}
