#include "ImageOutputWindow.h"
#include "WarpCalibrationOverlay.h"
#include "engine/ImageCue.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

ImageOutputWindow::ImageOutputWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Image Output — OQL");
    resize(1280, 720);

    m_calibOverlay = new WarpCalibrationOverlay(this);
    m_calibOverlay->setGeometry(rect());
    connect(m_calibOverlay, &WarpCalibrationOverlay::warpEdited,
            this, [this] { update(); emit calibrationEdited(); });
    connect(m_calibOverlay, &WarpCalibrationOverlay::warpCommitted,
            this, [this] { update(); emit calibrationCommitted(); });
}

QPixmap ImageOutputWindow::pixmapFor(const QString &path) {
    auto it = m_pixmapCache.find(path);
    if (it != m_pixmapCache.end()) {
        // Voce riusata: spostala in fondo alla coda di accesso (LRU).
        m_pixmapOrder.removeOne(path);
        m_pixmapOrder.append(path);
        return it.value();
    }
    QPixmap px(path);
    m_pixmapCache.insert(path, px);
    m_pixmapOrder.append(path);
    if (m_pixmapOrder.size() > kMaxCachedPixmaps) {
        const QString evict = m_pixmapOrder.takeFirst();
        m_pixmapCache.remove(evict);
    }
    return px;
}

void ImageOutputWindow::showCue(ImageCue *cue) {
    if (m_connection) disconnect(m_connection);
    m_cue = cue;
    if (!cue) { clearImage(); return; }
    m_connection = connect(cue, &Cue::displayChanged, this, &ImageOutputWindow::refresh);
    refresh();
    show();
    raise();
}

void ImageOutputWindow::clearImage() {
    if (m_connection) disconnect(m_connection);
    m_cue = nullptr;
    update();
}

void ImageOutputWindow::refresh() {
    update();
}

QImage ImageOutputWindow::composeFrame() const {
    const QSize sz = size();
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    if (sz.isEmpty() || !m_cue) return canvas;

    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Sfondo: colore pieno o immagine "expanding" (riempie tutta la
    // finestra, eventualmente ritagliata) — stesso comportamento di prima.
    if (m_cue->background() == ImageCue::Background::Image && !m_cue->backgroundImagePath().isEmpty()) {
        const QPixmap src = const_cast<ImageOutputWindow*>(this)->pixmapFor(m_cue->backgroundImagePath());
        if (!src.isNull()) {
            p.drawPixmap(canvas.rect(), src.scaled(
                sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
    } else {
        p.fillRect(canvas.rect(),
                   m_cue->background() == ImageCue::Background::White ? QColor(Qt::white)
                                                                         : QColor(Qt::black));
    }

    // Immagine corrente ("bottom"), centrata mantenendo le proporzioni.
    const QString bottomPath = m_cue->currentImagePath();
    if (!bottomPath.isEmpty()) {
        const QPixmap src = const_cast<ImageOutputWindow*>(this)->pixmapFor(bottomPath);
        if (!src.isNull()) {
            const QImage scaled = src.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
            const QPoint topLeft((sz.width() - scaled.width()) / 2, (sz.height() - scaled.height()) / 2);
            p.drawImage(topLeft, scaled);
        }
    }

    // Transizione verso la prossima immagine ("top"): crossfade o scorrimento.
    const bool transitioning = m_cue->isTransitioning()
                             && m_cue->transitionType() != ImageCue::Transition::Cut;
    if (transitioning) {
        const QString topPath = m_cue->nextImagePath();
        if (!topPath.isEmpty()) {
            const QPixmap src = const_cast<ImageOutputWindow*>(this)->pixmapFor(topPath);
            if (!src.isNull()) {
                const QImage scaled = src.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
                const QPoint centered((sz.width() - scaled.width()) / 2, (sz.height() - scaled.height()) / 2);
                const double progress = m_cue->transitionProgress();
                if (m_cue->transitionType() == ImageCue::Transition::Crossfade) {
                    p.setOpacity(progress);
                    p.drawImage(centered, scaled);
                    p.setOpacity(1.0);
                } else { // SlideHorizontal
                    const int x = int(sz.width() * (1.0 - progress));
                    p.drawImage(QPoint(x, centered.y()), scaled);
                }
            }
        }
    }

    // Fade a nero/bianco/immagine (stesso schema di VideoOutputWindow).
    const double overlayAlpha = 1.0 - m_cue->visualLevel();
    if (overlayAlpha > 0.001) {
        QColor c = Qt::black;
        p.setOpacity(overlayAlpha);
        p.fillRect(canvas.rect(), c);
        p.setOpacity(1.0);
    }
    return canvas;
}

void ImageOutputWindow::paintEvent(QPaintEvent *) {
    const QImage frame = composeFrame();
    QPainter painter(this);
    if (m_warp && !m_warp->isIdentity())
        painter.drawImage(0, 0, m_warp->renderWarped(frame, size()));
    else
        painter.drawImage(0, 0, frame);
}

void ImageOutputWindow::setProjectionWarp(ProjectionWarp *warp) {
    m_warp = warp;
    m_calibOverlay->setWarp(warp);
    update();
}

void ImageOutputWindow::setCalibrating(bool on) {
    m_calibOverlay->setCalibrating(on);
    update();
}

bool ImageOutputWindow::isCalibrating() const {
    return m_calibOverlay->isCalibrating();
}

void ImageOutputWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    m_calibOverlay->setGeometry(rect());
}

void ImageOutputWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void ImageOutputWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (isCalibrating()) { QWidget::mouseDoubleClickEvent(event); return; }
    toggleFullScreen();
}

void ImageOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
