#pragma once

#include <QElapsedTimer>

namespace Imap {

/**
 * Tiny utility timer for lightweight instrumentation in sync/logging paths.
 * Usage:
 *   KestrelTimer t;
 *   ...work...
 *   qInfo() << "elapsedMs=" << t.elapsed();
 */
class KestrelTimer {
public:
    KestrelTimer() { m_timer.start(); }

    [[nodiscard]] qint64 elapsed() const { return m_timer.elapsed(); }
    void restart() { m_timer.restart(); }

private:
    QElapsedTimer m_timer;
};

} // namespace Imap
