#include <QApplication>
#include "ui/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/Lv2Plugin.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("OpenQLab");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("OpenQLab");

    AudioEngine::instance().init();

    MainWindow w;
    w.show();

    const int ret = app.exec();
    AudioEngine::instance().shutdown();
    Lv2Plugin::freeWorld();
    return ret;
}
