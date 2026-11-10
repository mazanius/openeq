#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPixmap>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("openeq");
    QApplication::setApplicationDisplayName("openeq");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setQuitOnLastWindowClosed(false);
    theme::apply(app);

    // Single instance: a second launch just brings the first window forward.
    const QString serverName = "openeq-ui-" + qEnvironmentVariable("USERNAME");
    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    MainWindow window;
    if (const qsizetype demo = app.arguments().indexOf("--demo"); demo >= 0 && demo + 1 < app.arguments().size())
        window.loadDemo(app.arguments().at(demo + 1));
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            s->deleteLater();
            window.showFromTray();
        }
    });

    // Developer aid: render the window to a PNG and exit (used to check the UI in CI/docs).
    const qsizetype shot = app.arguments().indexOf("--screenshot");
    if (shot >= 0 && shot + 1 < app.arguments().size()) {
        const QString path = app.arguments().at(shot + 1);
        window.show();
        QTimer::singleShot(1200, &window, [&window, path] {
            window.grab().save(path);
            QCoreApplication::exit(0);
        });
        return app.exec();
    }

    if (!app.arguments().contains("--minimized")) window.show();
    return app.exec();
}
