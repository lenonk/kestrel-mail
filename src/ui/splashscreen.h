#pragma once

#include <QSplashScreen>
#include <QElapsedTimer>
#include <QStringList>

class QApplication;
class ImapService;

class SplashScreen : public QSplashScreen
{
    Q_OBJECT
public:
    explicit SplashScreen(const QPixmap &pixmap);

    // Run the splash loop until pool is ready or timeout. Pumps the event loop.
    void execUntilReady(QApplication &app, ImapService &imap);

private:
    void renderFrame(const QPixmap &base, const QString &status);

    QPixmap m_basePixmap;
    QElapsedTimer m_timer;
    int m_frame = 0;
    qint64 m_lastFrameMs = -75;
    QString m_currentStatus;
    qint64 m_nextStatusChangeMs = 0;

    static const QStringList &statusLines();
};
