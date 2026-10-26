#include "VideoOutputWindow.h"
#include "WarpCalibrationOverlay.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

VideoOutputWindow::VideoOutputWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Video Output — OQL");
    resize(1280, 720);

    // Il sink non è agganciato a un QVideoWidget: catturiamo i frame grezzi
    // per poterli comporre e deformare (warp) a mano in paintEvent — vedi
    // il commento in VideoOutputWindow.h sul perché.
    m_sink = new QVideoSink(this);
    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &VideoOutputWindow::onVideoFrameChanged);

    m_calibOverlay = new WarpCalibrationOverlay(this);
    m_calibOverlay->setGeometry(rect());
    connect(m_calibOverlay, &WarpCalibrationOverlay::warpEdited,
            this, [this] { update(); emit calibrationEdited(); });
    connect(m_calibOverlay, &WarpCalibrationOverlay::warpCommitted,
            this, [this] { update(); emit calibrationCommitted(); });
}

void VideoOutputWindow::onVideoFrameChanged(const QVideoFrame &frame) {
    if (frame.isValid())
        m_lastFrame = frame.toImage();
    update();
}

void VideoOutputWindow::setOpacity(double level) {
    m_opacity = qBound(0.0, level, 1.0);
    update();
}

void VideoOutputWindow::setBackground(Background mode, const QString &imagePath) {
    m_bgMode      = mode;
    m_bgImagePath = imagePath;
    update();
}

void VideoOutputWindow::setProjectionWarp(ProjectionWarp *warp) {
    m_warp = warp;
    m_calibOverlay->setWarp(warp);
    update();
}

void VideoOutputWindow::setCalibrating(bool on) {
    m_calibOverlay->setCalibrating(on);
    update();
}

bool VideoOutputWindow::isCalibrating() const {
    return m_calibOverlay->isCalibrating();
}

QImage VideoOutputWindow::composeFrame() const {
    const QSize sz = size();
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    if (sz.isEmpty()) return canvas;

    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!m_lastFrame.isNull()) {
        const QImage scaled = m_lastFrame.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint topLeft((sz.width() - scaled.width()) / 2, (sz.height() - scaled.height()) / 2);
        p.drawImage(topLeft, scaled);
    }

    // Overlay di fade verso nero/bianco/immagine: più opaco quanto più
    // m_opacity scende verso 0 — stesso comportamento dell'implementazione
    // precedente basata su QGraphicsOpacityEffect, solo composito a mano
    // perché ora tutto passa per un'unica QImage da (eventualmente) warpare.
    const double overlayAlpha = 1.0 - m_opacity;
    if (overlayAlpha > 0.001) {
        if (m_bgMode == Background::Image && !m_bgImagePath.isEmpty()) {
            QPixmap px;
            auto it = m_pixmapCache.constFind(m_bgImagePath);
            if (it != m_pixmapCache.constEnd()) {
                px = it.value();
            } else {
                px = QPixmap(m_bgImagePath);
                const_cast<VideoOutputWindow*>(this)->m_pixmapCache.insert(m_bgImagePath, px);
            }
            if (!px.isNull()) {
                p.setOpacity(overlayAlpha);
                p.drawPixmap(canvas.rect(), px.scaled(
                    sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                p.setOpacity(1.0);
            }
        } else {
            QColor c = (m_bgMode == Background::White) ? QColor(Qt::white) : QColor(Qt::black);
            c.setAlphaF(overlayAlpha);
            p.fillRect(canvas.rect(), c);
        }
    }
    return canvas;
}

void VideoOutputWindow::paintEvent(QPaintEvent *) {
    const QImage frame = composeFrame();
    QPainter painter(this);
    if (m_warp && !m_warp->isIdentity())
        painter.drawImage(0, 0, m_warp->renderWarped(frame, size()));
    else
        painter.drawImage(0, 0, frame);
}

void VideoOutputWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_calibOverlay->setGeometry(rect());
}

void VideoOutputWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void VideoOutputWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (isCalibrating()) { QWidget::mouseDoubleClickEvent(event); return; }
    toggleFullScreen();
}

void VideoOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
