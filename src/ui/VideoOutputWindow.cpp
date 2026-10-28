#include "VideoOutputWindow.h"
#include "engine/VideoCue.h"
#include "engine/Workspace.h"
#include "engine/CueList.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSet>

VideoOutputWindow::VideoOutputWindow(QWidget *parent, bool embedded)
    : QWidget(parent, embedded ? Qt::Widget : (Qt::Window | Qt::FramelessWindowHint)), m_embedded(embedded)
{
    if (!embedded) {
        setWindowTitle("Video Output — OQL");
        resize(1280, 720);
    }

    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(33); // ~30fps, coerente col vecchio ImageCue::m_timer
    connect(m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
}

void VideoOutputWindow::setOutput(Workspace *workspace, const QString &physicalOutputId) {
    if (m_workspace && m_workspace->cueList())
        disconnect(m_workspace->cueList(), nullptr, this, nullptr);

    m_workspace = workspace;
    m_outputId  = physicalOutputId;

    if (m_workspace && m_workspace->cueList()) {
        // Quale VideoCue è "attiva" per ciascuno schermo può cambiare in
        // qualunque momento (play/stop, riassegnazione degli schermi) — ad
        // ogni cambio si ricostruiscono i collegamenti frameChanged().
        connect(m_workspace->cueList(), &CueList::cueStateChanged, this,
                [this](int, Cue::State) { rescanActiveCueConnections(); });
        connect(m_workspace->cueList(), &CueList::cuePropertyChanged, this,
                [this](int) { rescanActiveCueConnections(); });
    }
    rescanActiveCueConnections();
}

void VideoOutputWindow::rescanActiveCueConnections() {
    for (const auto &c : m_cueFrameConnections) disconnect(c);
    m_cueFrameConnections.clear();

    if (m_workspace) {
        QSet<VideoCue*> seen;
        for (VirtualScreen *screen : m_workspace->virtualScreensFor(m_outputId)) {
            VideoCue *cue = m_workspace->cueList()->activeVideoCueForScreen(screen->id());
            if (!cue || seen.contains(cue)) continue;
            seen.insert(cue);
            m_cueFrameConnections.append(
                connect(cue, &VideoCue::frameChanged, this, QOverload<>::of(&QWidget::update)));
        }
    }
    update();
}

void VideoOutputWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_repaintTimer->start();
}

void VideoOutputWindow::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    m_repaintTimer->stop();
}

QImage VideoOutputWindow::composeCueFrame(VideoCue *cue) const {
    // Il frame NATIVO (risoluzione propria del video), non adattato a
    // nessuna finestra: l'adattamento (fitMode/canvasRect) lo decide
    // applyVirtualScreenMask() per ciascuno schermo — farlo qui prima
    // avrebbe "cotto" bande nere di letterbox nei pixel, rendendo il
    // fitMode per-schermo inefficace (bug osservato: cambiare l'adattamento
    // sembrava non avere alcun effetto visibile).
    // Niente conversione/copia qui: QImage è copy-on-write, quindi finché
    // non si disegna sopra (solo quando c'è davvero un fade in corso, vedi
    // sotto) `frame` condivide gli stessi pixel di cue->currentFrame() senza
    // allocazioni. Convertire formato a OGNI repaint (30 volte al secondo,
    // anche a video fermo sull'ultimo frame) era un costo pesante e per lo
    // più inutile — probabile causa degli scatti osservati.
    QImage frame = cue->currentFrame();
    if (frame.isNull()) return QImage();
    const QSize sz = frame.size();

    // Overlay di fade verso nero/bianco/immagine, con le proprietà lette
    // direttamente dalla cue (non più spinte dall'esterno con setBackground/
    // setOpacity): ogni schermo può così mostrare una cue diversa con il suo
    // proprio fade, invece di condividere un unico stato a livello finestra.
    const double overlayAlpha = 1.0 - cue->visualLevel();
    if (overlayAlpha > 0.001) {
        // La conversione serve solo ora che si disegna davvero sopra (un
        // formato non alpha-capable non reggerebbe il blend dell'overlay) —
        // scatta quindi solo durante un fade effettivo, non a ogni frame.
        if (frame.format() != QImage::Format_ARGB32_Premultiplied)
            frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&frame);
        if (cue->background() == VideoCue::Background::Image && !cue->backgroundImagePath().isEmpty()) {
            QPixmap px;
            auto it = m_pixmapCache.constFind(cue->backgroundImagePath());
            if (it != m_pixmapCache.constEnd()) {
                px = it.value();
            } else {
                px = QPixmap(cue->backgroundImagePath());
                m_pixmapCache.insert(cue->backgroundImagePath(), px);
            }
            if (!px.isNull()) {
                p.setOpacity(overlayAlpha);
                p.drawPixmap(frame.rect(), px.scaled(
                    sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                p.setOpacity(1.0);
            }
        } else {
            QColor c = (cue->background() == VideoCue::Background::White) ? QColor(Qt::white) : QColor(Qt::black);
            c.setAlphaF(overlayAlpha);
            p.fillRect(frame.rect(), c);
        }
    }
    return frame;
}

QImage VideoOutputWindow::composeCanvas() const {
    const QSize sz = size();
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    // Da incorporato (layer dentro CombinedOutputWindow) resta trasparente
    // dove non c'è nessuna cue attiva, così i layer sotto restano visibili
    // invece di essere coperti da un nero opaco — da finestra separata
    // resta nero come prima (nessuno sfondo dietro a cui "vedere attraverso").
    canvas.fill(m_embedded ? Qt::transparent : Qt::black);
    if (sz.isEmpty() || !m_workspace) return canvas;

    QPainter painter(&canvas);
    for (VirtualScreen *screen : m_workspace->virtualScreensFor(m_outputId)) {
        VideoCue *cue = m_workspace->cueList()->activeVideoCueForScreen(screen->id());
        if (!cue) continue;
        const QImage cueFrame = composeCueFrame(cue);
        if (cueFrame.isNull()) continue;
        // Disegna direttamente su `painter` (il canvas condiviso) invece di
        // allocare un buffer trasparente a parte per questo schermo e
        // ricopiarlo sopra — con più schermi quel costo si moltiplicava a
        // ogni frame ed era una causa concreta di scatti nella riproduzione.
        applyVirtualScreenMask(painter, cueFrame, sz, *screen);
    }
    return canvas;
}

void VideoOutputWindow::paintEvent(QPaintEvent *) {
    QImage canvas = composeCanvas();
    PhysicalOutput *out = m_workspace ? m_workspace->physicalOutputById(m_outputId) : nullptr;
    QPainter painter(this);
    if (out && !out->warp().isIdentity())
        painter.drawImage(0, 0, out->warp().renderWarped(canvas, size()));
    else
        painter.drawImage(0, 0, canvas);
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

void VideoOutputWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || window()->isFullScreen()) return;
    m_draggingWindow     = true;
    m_dragStartGlobalPos = event->globalPosition().toPoint();
    m_dragStartWindowPos = window()->pos();
}

void VideoOutputWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_draggingWindow) return;
    const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobalPos;
    window()->move(m_dragStartWindowPos + delta);
}

void VideoOutputWindow::mouseReleaseEvent(QMouseEvent *) {
    m_draggingWindow = false;
}

void VideoOutputWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else
        QWidget::keyPressEvent(event);
}
