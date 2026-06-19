#include "VideoOutputWindow.h"
#include <QLabel>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

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

    m_overlay = new QLabel(this);
    m_overlay->setAlignment(Qt::AlignCenter);
    m_overlay->setStyleSheet("background: black;");
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlayOpacity = new QGraphicsOpacityEffect(m_overlay);
    m_overlayOpacity->setOpacity(0.0);
    m_overlay->setGraphicsEffect(m_overlayOpacity);
    m_overlay->setGeometry(rect());
    m_overlay->raise();
}

void VideoOutputWindow::setOpacity(double level) {
    m_overlayOpacity->setOpacity(1.0 - qBound(0.0, level, 1.0));
}

void VideoOutputWindow::setBackground(Background mode, const QString &imagePath) {
    m_bgMode      = mode;
    m_bgImagePath = imagePath;
    refreshOverlayBackground();
}

void VideoOutputWindow::refreshOverlayBackground() {
    if (m_bgMode == Background::Image && !m_bgImagePath.isEmpty()) {
        auto it = m_pixmapCache.find(m_bgImagePath);
        const QPixmap src = (it != m_pixmapCache.end()) ? it.value()
                          : m_pixmapCache.insert(m_bgImagePath, QPixmap(m_bgImagePath)).value();
        if (!src.isNull()) {
            m_overlay->setStyleSheet("background: transparent;");
            m_overlay->setPixmap(src.scaled(
                size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            return;
        }
    }
    m_overlay->clear();
    m_overlay->setStyleSheet(m_bgMode == Background::White ? "background: white;"
                                                             : "background: black;");
}

void VideoOutputWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_overlay->setGeometry(rect());
    refreshOverlayBackground();
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
