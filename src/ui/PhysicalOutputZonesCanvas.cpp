#include "PhysicalOutputZonesCanvas.h"
#include "engine/Workspace.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QLineF>
#include <QApplication>
#include <QScreen>

PhysicalOutputZonesCanvas::PhysicalOutputZonesCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(260);
}

void PhysicalOutputZonesCanvas::setOutput(Workspace *workspace, const QString &physicalOutputId) {
    m_workspace = workspace;
    m_outputId  = physicalOutputId;
    m_selectedVertex = -1;
    m_dragging       = false;
    update();
}

void PhysicalOutputZonesCanvas::setSelectedScreen(const QString &screenId) {
    if (m_selectedScreenId == screenId) return;
    m_selectedScreenId = screenId;
    m_selectedVertex = -1;
    m_dragging       = false;
    update();
}

QRectF PhysicalOutputZonesCanvas::canvasPixelRect() const {
    const double margin = 16.0;
    const QRectF avail(margin, margin,
                         qMax(1.0, width()  - 2 * margin),
                         qMax(1.0, height() - 2 * margin));

    QSize res(1920, 1080);
    PhysicalOutput *out = m_workspace ? m_workspace->physicalOutputById(m_outputId) : nullptr;
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

    // Le proporzioni reali dello schermo rilevato/configurato, non stirate:
    // il rettangolo da dividere deve "sembrare" davvero lo schermo vero.
    const double arCanvas = double(res.width()) / double(res.height());
    const double arAvail  = avail.width() / avail.height();
    if (arAvail > arCanvas) {
        const double w = avail.height() * arCanvas;
        return QRectF(avail.left() + (avail.width() - w) / 2.0, avail.top(), w, avail.height());
    }
    const double h = avail.width() / arCanvas;
    return QRectF(avail.left(), avail.top() + (avail.height() - h) / 2.0, avail.width(), h);
}

QPointF PhysicalOutputZonesCanvas::toPixel(QPointF n, const QRectF &r) const {
    return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height());
}

QPointF PhysicalOutputZonesCanvas::toNormalized(QPointF px, const QRectF &r) const {
    return QPointF((px.x() - r.left()) / qMax(1.0, r.width()),
                    (px.y() - r.top())  / qMax(1.0, r.height()));
}

QColor PhysicalOutputZonesCanvas::colorForScreen(const QString &id) {
    static const QColor palette[] = {
        QColor(255, 210, 0), QColor(0, 200, 255), QColor(255, 110, 180),
        QColor(130, 255, 130), QColor(255, 150, 60), QColor(180, 140, 255),
        QColor(0, 230, 180), QColor(255, 90, 90),
    };
    const uint h = qHash(id);
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

void PhysicalOutputZonesCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 28));

    if (!m_workspace) return;
    const QRectF canvasRect = canvasPixelRect();

    p.setPen(QPen(QColor(90, 90, 96), 1.0));
    p.setBrush(QColor(40, 40, 46));
    p.drawRect(canvasRect);

    // Etichetta dell'output in filigrana: su una macchina con un solo
    // monitor, più output configurati sullo "schermo principale" hanno
    // canvas identici per proporzioni — questa etichetta toglie ogni dubbio
    // su quale output si sta davvero modellando in quel momento.
    if (PhysicalOutput *out = m_workspace->physicalOutputById(m_outputId)) {
        p.setPen(QColor(255, 255, 255, 40));
        QFont f = p.font();
        f.setPixelSize(qMax(14, int(canvasRect.height() * 0.06)));
        f.setBold(true);
        p.setFont(f);
        p.drawText(canvasRect, Qt::AlignCenter, out->name());
    }

    for (VirtualScreen *vs : m_workspace->virtualScreensFor(m_outputId)) {
        const QPolygonF poly = vs->maskPolygon();
        if (poly.size() < 3) continue;
        const bool selected = (vs->id() == m_selectedScreenId);

        QPolygonF pixelPoly;
        for (const QPointF &n : poly) pixelPoly << toPixel(n, canvasRect);

        QPainterPath path;
        path.addPolygon(pixelPoly);
        path.closeSubpath();

        const QColor base = colorForScreen(vs->id());
        QColor fill = base; fill.setAlpha(selected ? 70 : 38);
        QColor line = base; line.setAlpha(selected ? 235 : 150);
        p.setBrush(fill);
        p.setPen(QPen(line, selected ? 2.5 : 1.5));
        p.drawPath(path);

        p.setPen(QColor(255, 255, 255, selected ? 230 : 150));
        QFont f = p.font();
        f.setBold(selected);
        p.setFont(f);
        p.drawText(pixelPoly.boundingRect(), Qt::AlignCenter, vs->name());

        if (selected) {
            for (const QPointF &pp : pixelPoly) {
                p.setBrush(QColor(255, 255, 255));
                p.setPen(QPen(Qt::black, 1.5));
                p.drawEllipse(pp, 6.0, 6.0);
            }
        }
    }
}

void PhysicalOutputZonesCanvas::mousePressEvent(QMouseEvent *event) {
    if (!m_workspace) return;
    const QRectF canvasRect = canvasPixelRect();

    // 1) un handle del poligono GIA' selezionato ha priorità (per poterlo
    // trascinare anche se il click cade anche dentro un altro poligono
    // sovrapposto).
    VirtualScreen *selected = m_workspace->virtualScreenById(m_selectedScreenId);
    if (selected) {
        const QPolygonF poly = selected->maskPolygon();
        double bestDist = kHandleRadiusPx;
        int bestIdx = -1;
        for (int i = 0; i < poly.size(); ++i) {
            const double dist = QLineF(event->position(), toPixel(poly[i], canvasRect)).length();
            if (dist <= bestDist) { bestDist = dist; bestIdx = i; }
        }
        if (bestIdx >= 0) {
            m_selectedVertex = bestIdx;
            m_dragging = true;
            update();
            return;
        }
    }

    // 2) altrimenti, seleziona il poligono sotto il click (l'ultimo nella
    // lista che lo contiene, così quelli aggiunti dopo risultano "sopra").
    QString hit;
    for (VirtualScreen *vs : m_workspace->virtualScreensFor(m_outputId)) {
        QPolygonF pixelPoly;
        for (const QPointF &n : vs->maskPolygon()) pixelPoly << toPixel(n, canvasRect);
        if (pixelPoly.containsPoint(event->position(), Qt::OddEvenFill))
            hit = vs->id();
    }
    m_selectedVertex = -1;
    m_dragging = false;
    if (hit != m_selectedScreenId) {
        m_selectedScreenId = hit;
        emit selectionChangedByClick(hit);
    }
    update();
}

void PhysicalOutputZonesCanvas::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || m_selectedVertex < 0 || !m_workspace) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(m_selectedScreenId);
    if (!vs) return;
    const QRectF canvasRect = canvasPixelRect();
    QPolygonF poly = vs->maskPolygon();
    if (m_selectedVertex >= poly.size()) return;

    QPointF n = toNormalized(event->position(), canvasRect);
    n.setX(qBound(-0.25, n.x(), 1.25));
    n.setY(qBound(-0.25, n.y(), 1.25));
    poly[m_selectedVertex] = n;
    vs->setMaskPolygon(poly);
    update();
}

void PhysicalOutputZonesCanvas::mouseReleaseEvent(QMouseEvent *) {
    if (!m_dragging) return;
    m_dragging = false;
    emit maskEdited();
}

void PhysicalOutputZonesCanvas::addVertex() {
    if (!m_workspace) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(m_selectedScreenId);
    if (!vs) return;
    QPolygonF poly = vs->maskPolygon();
    if (poly.size() < 2) return;

    int bestEdge = 0;
    double bestLen = -1.0;
    for (int i = 0; i < poly.size(); ++i) {
        const double len = QLineF(poly[i], poly[(i + 1) % poly.size()]).length();
        if (len > bestLen) { bestLen = len; bestEdge = i; }
    }
    const QPointF mid = (poly[bestEdge] + poly[(bestEdge + 1) % poly.size()]) / 2.0;
    poly.insert(bestEdge + 1, mid);
    vs->setMaskPolygon(poly);
    m_selectedVertex = bestEdge + 1;
    update();
    emit maskEdited();
}

void PhysicalOutputZonesCanvas::removeSelectedVertex() {
    if (!m_workspace) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(m_selectedScreenId);
    if (!vs || m_selectedVertex < 0) return;
    QPolygonF poly = vs->maskPolygon();
    if (poly.size() <= 3 || m_selectedVertex >= poly.size()) return;
    poly.remove(m_selectedVertex);
    vs->setMaskPolygon(poly);
    m_selectedVertex = -1;
    update();
    emit maskEdited();
}

void PhysicalOutputZonesCanvas::resetPolygon() {
    if (!m_workspace) return;
    VirtualScreen *vs = m_workspace->virtualScreenById(m_selectedScreenId);
    if (!vs) return;
    vs->setMaskPolygon(QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0)));
    m_selectedVertex = -1;
    update();
    emit maskEdited();
}
