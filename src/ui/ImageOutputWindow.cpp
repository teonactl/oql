#include "ImageOutputWindow.h"
#include "engine/ImageCue.h"
#include "engine/Workspace.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>

ImageOutputWindow::ImageOutputWindow(QWidget *parent, bool embedded)
    : QWidget(parent, embedded ? Qt::Widget : (Qt::Window | Qt::FramelessWindowHint)), m_embedded(embedded)
{
    if (!embedded) {
        setWindowTitle("Image Output — OQL");
        resize(1280, 720);
    }

    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(33);
    connect(m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
}

void ImageOutputWindow::setOutput(Workspace *workspace, const QString &physicalOutputId) {
    m_workspace = workspace;
    m_outputId  = physicalOutputId;
    update();
}

void ImageOutputWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_repaintTimer->start();
}

void ImageOutputWindow::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    m_repaintTimer->stop();
}

QPixmap ImageOutputWindow::pixmapFor(const QString &path) const {
    auto it = m_pixmapCache.find(path);
    if (it != m_pixmapCache.end()) {
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

QImage ImageOutputWindow::composeCueFrame(ImageCue *cue) const {
    // Nessun pre-adattamento a una dimensione di finestra qui — vedi il
    // commento in VideoOutputWindow::composeCueFrame() sul perché
    // (altrimenti il fitMode per-schermo lavorerebbe su bande di letterbox
    // già "cotte" nei pixel invece che sul contenuto vero). Per colore/
    // gradiente non esiste una risoluzione nativa: si usa una dimensione di
    // lavoro fissa, tanto fitMode la adatta comunque a qualunque zona.
    if (cue->contentMode() != ImageCue::ContentMode::File) {
        const QSize sz(1920, 1080);
        QImage canvas = cue->renderGeneratedContent(sz);
        const double overlayAlpha = 1.0 - cue->visualLevel();
        if (overlayAlpha > 0.001) {
            QPainter p(&canvas);
            p.setOpacity(overlayAlpha);
            p.fillRect(canvas.rect(), Qt::black);
        }
        return canvas;
    }

    // Per una slideshow da file, la dimensione "nativa" di lavoro è quella
    // dell'immagine corrente ("bottom"): riempie così esattamente il canvas
    // di composizione senza bande, e sfondo/transizione si adattano a lei.
    const QString bottomPath = cue->currentImagePath();
    if (bottomPath.isEmpty()) return QImage();
    const QPixmap bottomPx = pixmapFor(bottomPath);
    if (bottomPx.isNull()) return QImage();
    const QSize sz = bottomPx.size();

    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (cue->background() == ImageCue::Background::Image && !cue->backgroundImagePath().isEmpty()) {
        const QPixmap src = pixmapFor(cue->backgroundImagePath());
        if (!src.isNull()) {
            p.drawPixmap(canvas.rect(), src.scaled(
                sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        }
    } else {
        p.fillRect(canvas.rect(),
                   cue->background() == ImageCue::Background::White ? QColor(Qt::white)
                                                                       : QColor(Qt::black));
    }

    p.drawPixmap(0, 0, bottomPx);

    const bool transitioning = cue->isTransitioning()
                             && cue->transitionType() != ImageCue::Transition::Cut;
    if (transitioning) {
        const QString topPath = cue->nextImagePath();
        if (!topPath.isEmpty()) {
            const QPixmap src = pixmapFor(topPath);
            if (!src.isNull()) {
                const QImage scaled = src.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
                const QPoint centered((sz.width() - scaled.width()) / 2, (sz.height() - scaled.height()) / 2);
                const double progress = cue->transitionProgress();
                if (cue->transitionType() == ImageCue::Transition::Crossfade) {
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

    const double overlayAlpha = 1.0 - cue->visualLevel();
    if (overlayAlpha > 0.001) {
        p.setOpacity(overlayAlpha);
        p.fillRect(canvas.rect(), Qt::black);
        p.setOpacity(1.0);
    }
    return canvas;
}

QImage ImageOutputWindow::composeCanvas() const {
    const QSize sz = size();
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    // Vedi lo stesso commento in VideoOutputWindow::composeCanvas(): da
    // incorporato resta trasparente dove non c'è nessuna cue immagine
    // attiva, per non coprire il video sotto.
    canvas.fill(m_embedded ? Qt::transparent : Qt::black);
    if (sz.isEmpty() || !m_workspace) return canvas;

    QPainter painter(&canvas);
    for (VirtualScreen *screen : m_workspace->virtualScreensFor(m_outputId)) {
        ImageCue *cue = m_workspace->cueList()->activeImageCueForScreen(screen->id());
        if (!cue) continue;
        const QImage cueFrame = composeCueFrame(cue);
        if (cueFrame.isNull()) continue;
        // Vedi lo stesso commento in VideoOutputWindow::composeCanvas().
        applyVirtualScreenMask(painter, cueFrame, sz, *screen);
    }
    return canvas;
}

void ImageOutputWindow::paintEvent(QPaintEvent *) {
    QImage canvas = composeCanvas();
    PhysicalOutput *out = m_workspace ? m_workspace->physicalOutputById(m_outputId) : nullptr;
    QPainter painter(this);
    if (out && !out->warp().isIdentity())
        painter.drawImage(0, 0, out->warp().renderWarped(canvas, size()));
    else
        painter.drawImage(0, 0, canvas);
}

void ImageOutputWindow::toggleFullScreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void ImageOutputWindow::mouseDoubleClickEvent(QMouseEvent *) {
    toggleFullScreen();
}

void ImageOutputWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || window()->isFullScreen()) return;
    m_draggingWindow     = true;
    m_dragStartGlobalPos = event->globalPosition().toPoint();
    m_dragStartWindowPos = window()->pos();
}

void ImageOutputWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_draggingWindow) return;
    const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobalPos;
    window()->move(m_dragStartWindowPos + delta);
}

void ImageOutputWindow::mouseReleaseEvent(QMouseEvent *) {
    m_draggingWindow = false;
}

void ImageOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
