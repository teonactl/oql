#include "ScreenFitPreview.h"
#include "engine/Workspace.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QDebug>

// Diagnostica opzionale per il trascinamento, attivabile con OQL_FIT_DEBUG=1
// nell'ambiente — stampa su stderr cosa calcola mousePressEvent/
// mouseMoveEvent, per verificare empiricamente dove si inceppa invece di
// continuare a modificare la formula alla cieca.
static bool fitDebugEnabled() {
    static const bool enabled = qEnvironmentVariableIntValue("OQL_FIT_DEBUG") != 0;
    return enabled;
}

ScreenFitPreview::ScreenFitPreview(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(220);
}

void ScreenFitPreview::setTarget(Workspace *workspace, const QString &screenId,
                                  std::function<QImage()> contentProvider) {
    m_workspace = workspace;
    m_screenId = screenId;
    m_contentProvider = std::move(contentProvider);
    m_dragging = false;
    update();
}

void ScreenFitPreview::refresh() { update(); }

void ScreenFitPreview::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

QRectF ScreenFitPreview::canvasPixelRect() const {
    const double margin = 8.0;
    const QRectF avail(margin, margin, qMax(1.0, width() - 2 * margin), qMax(1.0, height() - 2 * margin));

    QSize res(1920, 1080);
    VirtualScreen *vs = m_workspace ? m_workspace->virtualScreenById(m_screenId) : nullptr;
    PhysicalOutput *out = (m_workspace && vs) ? m_workspace->physicalOutputById(vs->physicalOutputId()) : nullptr;
    if (out) {
        if (out->autoResolution()) {
            QScreen *target = nullptr;
            if (!out->physicalScreenName().isEmpty()) {
                for (QScreen *s : QApplication::screens())
                    if (s->name() == out->physicalScreenName()) { target = s; break; }
            }
            if (!target) target = QApplication::primaryScreen();
            if (target) res = target->geometry().size();
        } else {
            res = out->resolution();
        }
    }
    if (res.isEmpty()) res = QSize(1920, 1080);

    const double arCanvas = double(res.width()) / double(res.height());
    const double arAvail  = avail.width() / avail.height();
    if (arAvail > arCanvas) {
        const double w = avail.height() * arCanvas;
        return QRectF(avail.left() + (avail.width() - w) / 2.0, avail.top(), w, avail.height());
    }
    const double h = avail.width() / arCanvas;
    return QRectF(avail.left(), avail.top() + (avail.height() - h) / 2.0, avail.width(), h);
}

ScreenFitPreview::FitGeometry ScreenFitPreview::currentFitGeometry(const QRectF &canvasRect, VirtualScreen *vs) const {
    const QSize canvasSizeI = canvasRect.size().toSize();
    const QImage content = m_contentProvider ? m_contentProvider() : QImage();

    QSizeF croppedSize = kPlaceholderContentSize;
    if (!content.isNull()) {
        const QRectF cr = vs->canvasRect();
        const QRect cropPx(
            qRound(cr.x() * content.width()),     qRound(cr.y() * content.height()),
            qRound(cr.width() * content.width()), qRound(cr.height() * content.height()));
        const QRect validCrop = cropPx.intersected(content.rect());
        croppedSize = validCrop.isEmpty() ? QSizeF(content.size()) : QSizeF(validCrop.size());
    }

    const QRectF targetPx = virtualScreenTargetRect(*vs, canvasSizeI);
    const QRectF destRect = fitDestRect(croppedSize, targetPx, vs->fitMode(), vs->contentAnchor(), vs->zoom());
    return FitGeometry{targetPx, destRect};
}

void ScreenFitPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(20, 20, 24));

    const QRectF canvasRect = canvasPixelRect();
    p.setPen(QPen(QColor(90, 90, 96), 1.0));
    p.setBrush(QColor(10, 10, 12));
    p.drawRect(canvasRect);

    VirtualScreen *vs = m_workspace ? m_workspace->virtualScreenById(m_screenId) : nullptr;
    if (!vs) return;
    const QSize canvasSizeI = canvasRect.size().toSize();
    if (canvasSizeI.isEmpty()) return;

    const QImage content = m_contentProvider ? m_contentProvider() : QImage();
    if (!content.isNull()) {
        // La stessa funzione usata dalla resa reale (vedi VideoOutputWindow/
        // ImageOutputWindow): l'anteprima non può disallinearsi da essa.
        const QImage masked = applyVirtualScreenMask(content, canvasSizeI, *vs);
        p.drawImage(canvasRect.topLeft(), masked);
    } else {
        // Nessun frame disponibile (video non in riproduzione, immagine non
        // ancora caricata): un riquadro segnaposto, posizionato con la
        // STESSA logica fitMode/anchor che userebbe un contenuto vero — così
        // la posizione si può comunque regolare trascinando, senza dover
        // avviare la cue solo per vedere dove cade l'ancora (vedi anche
        // mousePressEvent, che usa lo stesso segnaposto per il trascinamento).
        const QRectF targetPx = virtualScreenTargetRect(*vs, canvasSizeI);
        const QRectF placeholderRect = fitDestRect(kPlaceholderContentSize, targetPx, vs->fitMode(), vs->contentAnchor(), vs->zoom())
            .translated(canvasRect.topLeft());
        p.setPen(QPen(QColor(150, 150, 150), 1.0, Qt::DashLine));
        p.setBrush(QColor(150, 150, 150, 40));
        p.drawRect(placeholderRect);
        p.setPen(QColor(190, 190, 190));
        p.drawText(placeholderRect, Qt::AlignCenter,
                   tr("anteprima non disponibile\n(avvia la cue per vederla)"));
    }

    const QPolygonF poly = vs->maskPolygon();
    if (poly.size() >= 3) {
        QPolygonF pixelPoly;
        pixelPoly.reserve(poly.size());
        for (const QPointF &n : poly)
            pixelPoly << QPointF(canvasRect.left() + n.x() * canvasRect.width(),
                                   canvasRect.top()  + n.y() * canvasRect.height());
        p.setPen(QPen(QColor(255, 210, 0, 210), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(pixelPoly);
    }
}

void ScreenFitPreview::mousePressEvent(QMouseEvent *event) {
    VirtualScreen *vs = m_workspace ? m_workspace->virtualScreenById(m_screenId) : nullptr;
    if (!vs) {
        if (fitDebugEnabled()) qWarning().noquote() << "[fit-debug] mousePress: nessun VirtualScreen (screenId=" << m_screenId << ")";
        return;
    }
    const QRectF canvasRect = canvasPixelRect();
    const QSize canvasSizeI = canvasRect.size().toSize();
    if (canvasSizeI.isEmpty()) {
        if (fitDebugEnabled()) qWarning().noquote() << "[fit-debug] mousePress: canvasPixelRect vuoto, widget size=" << size();
        return;
    }

    const FitGeometry fg = currentFitGeometry(canvasRect, vs);
    const QRectF targetPxWidget = fg.targetPx.translated(canvasRect.topLeft());
    if (!targetPxWidget.contains(event->position())) {
        if (fitDebugEnabled())
            qWarning().noquote() << QStringLiteral("[fit-debug] mousePress FUORI dalla zona: click=%1 targetPxWidget=%2")
                .arg(QStringLiteral("%1,%2").arg(event->position().x()).arg(event->position().y()))
                .arg(QStringLiteral("%1,%2 %3x%4").arg(targetPxWidget.x()).arg(targetPxWidget.y()).arg(targetPxWidget.width()).arg(targetPxWidget.height()));
        return;
    }

    const double rawSlackW = fg.targetPx.width()  - fg.destRect.width();
    const double rawSlackH = fg.targetPx.height() - fg.destRect.height();

    if (fitDebugEnabled())
        qWarning().noquote() << QStringLiteral("[fit-debug] mousePress OK: fitMode=%1 zoom=%2 targetPx=%3x%4 destRect=%5x%6 slack=%7,%8")
            .arg(int(vs->fitMode())).arg(vs->zoom())
            .arg(fg.targetPx.width()).arg(fg.targetPx.height())
            .arg(fg.destRect.width()).arg(fg.destRect.height())
            .arg(rawSlackW).arg(rawSlackH);

    m_dragging        = true;
    m_dragStartAnchor = vs->contentAnchor();
    m_dragStartMouse  = event->position().toPoint();
    m_dragRange = fg.targetPx.size();
    // Segno: 0 quando quell'asse non ha davvero nessun grado di libertà
    // (Stirato, o una coincidenza esatta di proporzioni) — in quel caso
    // trascinare non cambierebbe comunque nulla di visibile, meglio non
    // far deragliare contentAnchor senza motivo. Altrimenti +1 se il
    // contenuto è più piccolo del target su quell'asse (trascinare a
    // destra/giù lo sposta a destra/giù), -1 se è più grande (trascinare a
    // destra/giù rivela la parte destra/bassa spostando il contenuto verso
    // sinistra/alto).
    m_dragSign = QPointF(qAbs(rawSlackW) < 0.5 ? 0.0 : (rawSlackW < 0 ? -1.0 : 1.0),
                          qAbs(rawSlackH) < 0.5 ? 0.0 : (rawSlackH < 0 ? -1.0 : 1.0));
    update();
}

void ScreenFitPreview::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) return;
    VirtualScreen *vs = m_workspace ? m_workspace->virtualScreenById(m_screenId) : nullptr;
    if (!vs) return;

    const QPoint delta = event->position().toPoint() - m_dragStartMouse;
    double ax = m_dragStartAnchor.x();
    double ay = m_dragStartAnchor.y();
    // La sensibilità è legata alla dimensione della ZONA (m_dragRange, vedi
    // il commento in ScreenFitPreview.h), non al margine effettivo: così
    // resta prevedibile — trascinare da un capo all'altro della zona
    // percorre sempre tutto l'anchor 0..1 — indipendentemente da quanto
    // margine reale c'è davvero, incluso con lo zoom di Manuale che lo fa
    // crescere molto.
    if (m_dragRange.width()  > 0.5) ax = m_dragStartAnchor.x() + m_dragSign.x() * delta.x() / m_dragRange.width();
    if (m_dragRange.height() > 0.5) ay = m_dragStartAnchor.y() + m_dragSign.y() * delta.y() / m_dragRange.height();
    const QPointF newAnchor(qBound(0.0, ax, 1.0), qBound(0.0, ay, 1.0));
    if (fitDebugEnabled())
        qWarning().noquote() << QStringLiteral("[fit-debug] mouseMove: delta=%1,%2 sign=%3,%4 range=%5x%6 anchor %7,%8 -> %9,%10")
            .arg(delta.x()).arg(delta.y()).arg(m_dragSign.x()).arg(m_dragSign.y())
            .arg(m_dragRange.width()).arg(m_dragRange.height())
            .arg(m_dragStartAnchor.x()).arg(m_dragStartAnchor.y())
            .arg(newAnchor.x()).arg(newAnchor.y());
    vs->setContentAnchor(newAnchor);
    update();
}

void ScreenFitPreview::mouseReleaseEvent(QMouseEvent *) {
    if (!m_dragging) return;
    m_dragging = false;
    emit anchorEdited();
}
