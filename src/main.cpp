#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QWebEngineSettings>

#include <QtWebEngineCore/qtwebenginecoreglobal.h>

#include "app/applicationcontroller.hpp"
#include "browser/mochurlscheme.hpp"

#ifndef MOCH_VERSION
#define MOCH_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[])
{
    MochUrlScheme::registerScheme();

    QApplication app(argc, argv);
    QApplication::setApplicationName("moch");
    QApplication::setOrganizationName("moch");
    QApplication::setApplicationVersion(QStringLiteral(MOCH_VERSION " (Qt %1, Chromium %2)")
                                            .arg(QString::fromLatin1(qVersion()),
                                                 QString::fromLatin1(qWebEngineChromiumVersion())));

    QCommandLineParser parser;
    parser.setApplicationDescription("Moch — an AI-native browser");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selftestOption("selftest", "Exit after successful startup (for CI).");
    parser.addOption(selftestOption);
    parser.process(app);

    ApplicationController controller;
    controller.start();

    if (parser.isSet(selftestOption)) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }
    return app.exec();
}
