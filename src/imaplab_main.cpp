#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "tools/imaplabcontroller.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    ImapLabController controller(&engine);
    engine.rootContext()->setContextProperty("imapLab", &controller);

    const QUrl url(QStringLiteral("qrc:/qml/ImapLab.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
