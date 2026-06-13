#include "VideoOutputWindow.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>

VideoOutputWindow::VideoOutputWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Video Output — OQL");
    setStyleSheet("background: black;");
    resize(1280, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setStyleSheet("background: black;");
    layout->addWidget(m_videoWidget);
}

void VideoOutputWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void VideoOutputWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void VideoOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
