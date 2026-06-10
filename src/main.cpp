#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/ScriptEngine.h"
#include "engine/Lv2Plugin.h"
#include <cstdlib>

int main(int argc, char *argv[]) {
    // VST/LV2 native editors require X11 window embedding (XCreateWindow with
    // a real X11 parent). On Wayland, Qt's winId() creates windows through a
    // path that cannot be synced via QNativeInterface::QX11Application, so DPF/
    // Pugl sees BadWindow on the parent before the X server processes it.
    // Force xcb (X11 via XWayland) so Qt uses a single XCB connection we can
    // sync with XSync before passing the window ID to the plugin.
    if (!getenv("QT_QPA_PLATFORM"))
        setenv("QT_QPA_PLATFORM", "xcb", 0);

    QApplication app(argc, argv);
    app.setApplicationName("OpenQLab");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("OpenQLab");
    app.setWindowIcon(QIcon(":/icons/openqlab.svg"));

    AudioEngine::instance().init();

    int ret;
    {
        MainWindow w;
        w.show();
        ret = app.exec();
        // MainWindow (and all cues) destruct here, before engine teardown
    }

    // Shut down engines only after all cues/renderers are gone
    AudioEngine::instance().shutdown();
    ScriptEngine::instance().shutdown();
    Lv2Plugin::freeWorld();
    return ret;
}
