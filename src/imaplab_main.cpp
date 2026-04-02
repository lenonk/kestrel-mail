#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "tools/imaplabcontroller.h"

using namespace Qt::Literals::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    ImapLabController controller(&engine);
    engine.rootContext()->setContextProperty("imapLab", &controller);

    const QUrl url("qrc:/qml/ImapLab.qml"_L1);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
