#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QByteArray>
#include <QDate>
#include <QMessageBox>
#include "ui/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/AppSettings.h"
#include "engine/ScriptEngine.h"
#include "engine/Lv2Plugin.h"
#include <cstdlib>

static void applyDarkTheme(QApplication &app)
{
    QPalette pal;
    const QColor bg    (0x0f, 0x11, 0x17);
    const QColor card  (0x1e, 0x23, 0x34);
    const QColor text  (0xe2, 0xe8, 0xf0);
    const QColor muted (0x88, 0x92, 0xa4);
    const QColor border(0x2a, 0x30, 0x50);
    const QColor accent(0x4f, 0x8e, 0xf7);

    pal.setColor(QPalette::Window,          bg);
    pal.setColor(QPalette::WindowText,      text);
    pal.setColor(QPalette::Base,            bg);
    pal.setColor(QPalette::AlternateBase,   card);
    pal.setColor(QPalette::Text,            text);
    pal.setColor(QPalette::BrightText,      Qt::white);
    pal.setColor(QPalette::Button,          card);
    pal.setColor(QPalette::ButtonText,      text);
    pal.setColor(QPalette::Highlight,       accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase,     card);
    pal.setColor(QPalette::ToolTipText,     text);
    pal.setColor(QPalette::Dark,            QColor(0x0a, 0x0c, 0x14));
    pal.setColor(QPalette::Mid,             QColor(0x16, 0x19, 0x24));
    pal.setColor(QPalette::Midlight,        border);
    pal.setColor(QPalette::Shadow,          QColor(0x05, 0x07, 0x0e));
    pal.setColor(QPalette::Link,            accent);
    pal.setColor(QPalette::PlaceholderText, muted);
    app.setPalette(pal);

    app.setStyleSheet(QStringLiteral(
        "QMainWindow { background:#0f1117; }"
        "QMainWindow::separator { background:#2a3050; width:1px; height:1px; }"
        "QMenuBar { background:#0f1117; color:#e2e8f0; border-bottom:1px solid #2a3050; }"
        "QMenuBar::item { padding:4px 10px; background:transparent; }"
        "QMenuBar::item:selected { background:#1e2334; border-radius:3px; }"
        "QMenu { background:#1e2334; border:1px solid #2a3050; color:#e2e8f0; padding:4px; }"
        "QMenu::item { padding:5px 20px 5px 10px; border-radius:3px; }"
        "QMenu::item:selected { background:#4f8ef7; color:white; }"
        "QMenu::separator { height:1px; background:#2a3050; margin:3px 6px; }"
        "QToolBar { background:#12151f; border:none; border-bottom:1px solid #2a3050; spacing:3px; padding:2px 4px; }"
        "QToolBar::separator { background:#2a3050; width:1px; margin:4px 2px; }"
        "QToolButton { color:#e2e8f0; border:none; border-radius:5px; padding:3px; }"
        "QToolButton:hover { background:rgba(255,255,255,14); }"
        "QToolButton:pressed { background:rgba(0,0,0,30); }"
        "QToolButton:checked { background:rgba(79,142,247,30); border:2px solid rgba(79,142,247,200); border-radius:5px; }"
        "QTableView { background:#0f1117; alternate-background-color:#0f1117;"
        "  border:none; gridline-color:transparent;"
        "  selection-background-color:transparent; selection-color:#e2e8f0; }"
        "QTableView::item { background:transparent; border:none; color:#e2e8f0; }"
        "QTableView::item:selected { background:transparent; }"
        "QHeaderView { background:#0f1117; border:none; }"
        "QHeaderView::section { background:#0f1117; color:#8892a4; border:none;"
        "  border-bottom:1px solid #2a3050; padding:4px 8px; }"
        "QScrollBar:vertical { background:#0f1117; width:6px; border-radius:3px; margin:0; }"
        "QScrollBar::handle:vertical { background:#2a3050; border-radius:3px; min-height:24px; }"
        "QScrollBar::handle:vertical:hover { background:#4f8ef7; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar:horizontal { background:#0f1117; height:6px; border-radius:3px; margin:0; }"
        "QScrollBar::handle:horizontal { background:#2a3050; border-radius:3px; min-width:24px; }"
        "QScrollBar::handle:horizontal:hover { background:#4f8ef7; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }"
        "QSplitter::handle { background:#2a3050; }"
        "QSplitter::handle:horizontal { width:1px; }"
        "QSplitter::handle:vertical { height:1px; }"
        "QStatusBar { background:#12151f; border-top:1px solid #2a3050; color:#8892a4; }"
        "QDockWidget { background:#0f1117; color:#e2e8f0; }"
        "QDockWidget::title { background:#12151f; padding:5px 8px; border-bottom:1px solid #2a3050; }"
        "QDockWidget::close-button, QDockWidget::float-button { border:none; background:transparent; }"
        "QTabWidget::pane { border:1px solid #2a3050; background:#0f1117; }"
        "QTabBar::tab { background:#0f1117; color:#8892a4; border:none; padding:6px 16px; margin-right:1px; }"
        "QTabBar::tab:selected { color:#e2e8f0; border-bottom:2px solid #4f8ef7; }"
        "QTabBar::tab:hover { color:#e2e8f0; background:#1e2334; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {"
        "  background:#1e2334; border:1px solid #2a3050; color:#e2e8f0;"
        "  border-radius:4px; padding:2px 6px; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {"
        "  border-color:#4f8ef7; }"
        "QSpinBox::up-button, QSpinBox::down-button,"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  width:16px; background:#252d40; border:none; border-left:1px solid #1c2040; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover,"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background:#3a4060; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "  image:url(:/icons/arrow-up.svg); width:7px; height:5px; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "  image:url(:/icons/arrow-down.svg); width:7px; height:5px; }"
        "QComboBox { background:#1e2334; border:1px solid #2a3050; color:#e2e8f0;"
        "  border-radius:4px; padding:2px 6px; }"
        "QComboBox:focus { border-color:#4f8ef7; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:#1e2334; border:1px solid #2a3050;"
        "  color:#e2e8f0; selection-background-color:#4f8ef7; selection-color:white; }"
        "QGroupBox { border:1px solid #2a3050; border-radius:6px; margin-top:10px;"
        "  color:#8892a4; padding-top:4px; }"
        "QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left; padding:0 5px; }"
        "QCheckBox { color:#e2e8f0; spacing:6px; }"
        "QCheckBox::indicator { width:14px; height:14px; border:1px solid #2a3050;"
        "  border-radius:3px; background:#1e2334; }"
        "QCheckBox::indicator:checked { background:#4f8ef7; border-color:#4f8ef7; }"
        "QRadioButton { color:#e2e8f0; spacing:6px; }"
        "QSlider::groove:horizontal { background:#2a3050; height:4px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:#4f8ef7; width:14px; height:14px;"
        "  border-radius:7px; margin:-5px 0; }"
        "QSlider::groove:vertical { background:#2a3050; width:4px; border-radius:2px; }"
        "QSlider::handle:vertical { background:#4f8ef7; width:14px; height:14px;"
        "  border-radius:7px; margin:0 -5px; }"
        "QPushButton { background:#1e2334; color:#e2e8f0; border:1px solid #2a3050;"
        "  border-radius:5px; padding:4px 14px; }"
        "QPushButton:hover { background:#26304a; }"
        "QPushButton:pressed { background:#16192a; }"
        "QDialog { background:#0f1117; }"
        "QLabel { color:#e2e8f0; background:transparent; }"
        "QFrame[frameShape='4'], QFrame[frameShape='5'] { color:#2a3050; }"
    ));
}

int main(int argc, char *argv[]) {
#ifdef Q_OS_LINUX
    // VST/LV2 native editors require X11 window embedding. Force xcb so Qt
    // uses a single XCB connection we can sync with XSync before passing the
    // window ID to the plugin (Wayland's winId() path triggers BadWindow).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif

    QApplication app(argc, argv);
    app.setApplicationName("OQL");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("OQL");
    app.setWindowIcon(QIcon(":/icons/openqlab.svg"));
    applyDarkTheme(app);

    AppSettings::applyLanguage();

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
