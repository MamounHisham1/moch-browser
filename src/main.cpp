#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QWebEngineSettings>

#include "app/applicationcontroller.hpp"
#include "browser/mochurlscheme.hpp"

int main(int argc, char *argv[])
{
    MochUrlScheme::registerScheme();

    QApplication app(argc, argv);
    QApplication::setApplicationName("moch");
    QApplication::setOrganizationName("moch");
    QApplication::setApplicationVersion("0.1.0");

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
