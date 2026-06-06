#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("OpenQLab");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("OpenQLab");

    MainWindow w;
    w.show();

    return app.exec();
}
