#include "WarpCalibrationOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QLineF>

WarpCalibrationOverlay::WarpCalibrationOverlay(QWidget *parent) : QWidget(parent) {
    // Vive solo dentro ProjectionCalibrationWindow, che esiste solo per
    // calibrare: sempre attivo, niente stato "calibrating" da commutare.
    setAutoFillBackground(false);
}

void WarpCalibrationOverlay::setWarp(ProjectionWarp *warp) {
    m_warp = warp;
    update();
}

QPointF WarpCalibrationOverlay::handlePixelPos(int col, int row) const {
    const QPointF n = m_warp->controlPoint(col, row);
    double x = n.x() * width();
    double y = n.y() * height();
    if (col == 0 || col == m_warp->cols())
        x = qBound(kEdgeInsetPx, x, double(width())  - kEdgeInsetPx);
    if (row == 0 || row == m_warp->rows())
        y = qBound(kEdgeInsetPx, y, double(height()) - kEdgeInsetPx);
    return QPointF(x, y);
}

void WarpCalibrationOverlay::paintEvent(QPaintEvent *) {
    if (!m_warp) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int cols = m_warp->cols();
    const int rows = m_warp->rows();
    const double w = width();
    const double h = height();

    auto truePx = [&](int c, int r) {
        const QPointF n = m_warp->controlPoint(c, r);
        return QPointF(n.x() * w, n.y() * h);
    };

    // Linee della mesh: polilinea attraverso la griglia DENSA (spline di
    // Catmull-Rom, vedi ProjectionWarp::smoothGrid) invece che un segmento
    // retto tra due punti di controllo consecutivi. Una trasformazione
    // proiettiva per-cella mappa rette in rette: disegnando solo i punti
    // grezzi, la griglia appare "spezzata" (pendenza diversa da un lato e
    // dall'altro di ogni punto interno) invece che curva continua — qui si
    // mostra invece esattamente la stessa curva che renderWarped() userà per
    // deformare il contenuto vero, utile quando si calibra su una superficie
    // non piatta.
    int denseCols = cols, denseRows = rows;
    const QVector<QPointF> &grid = m_warp->smoothGrid(&denseCols, &denseRows);
    const int subdiv = m_warp->smoothSubdiv();
    auto densePx = [&](int dc, int dr) {
        const QPointF n = grid[dr * (denseCols + 1) + dc];
        return QPointF(n.x() * w, n.y() * h);
    };

    QPen linePen(QColor(255, 210, 0, 200));
    linePen.setWidthF(1.5);
    p.setPen(linePen);
    for (int r = 0; r <= rows; ++r) {
        QPolygonF poly;
        for (int dc = 0; dc <= denseCols; ++dc) poly << densePx(dc, r * subdiv);
        p.drawPolyline(poly);
    }
    for (int c = 0; c <= cols; ++c) {
        QPolygonF poly;
        for (int dr = 0; dr <= denseRows; ++dr) poly << densePx(c * subdiv, dr);
        p.drawPolyline(poly);
    }

    // Handle ai punti di controllo: alla posizione inset (facile da vedere e
    // afferrare anche quando il punto vero è sul bordo della finestra), con
    // un piccolo segmento che lo collega alla posizione vera se sono diverse.
    for (int r = 0; r <= rows; ++r) {
        for (int c = 0; c <= cols; ++c) {
            const QPointF truePos   = truePx(c, r);
            const QPointF handlePos = handlePixelPos(c, r);
            if (QLineF(truePos, handlePos).length() > 1.0) {
                QPen stub(QColor(255, 210, 0, 120));
                stub.setStyle(Qt::DashLine);
                p.setPen(stub);
                p.drawLine(truePos, handlePos);
            }
            const bool active = (m_dragging && c == m_dragCol && r == m_dragRow);
            p.setBrush(active ? QColor(255, 255, 255) : QColor(255, 210, 0));
            p.setPen(QPen(Qt::black, 1.5));
            const double radius = active ? 8.0 : 6.0;
            p.drawEllipse(handlePos, radius, radius);
        }
    }
}

void WarpCalibrationOverlay::mousePressEvent(QMouseEvent *event) {
    if (!m_warp) return;
    double bestDist = kHandleRadiusPx;
    int bestCol = -1, bestRow = -1;
    for (int r = 0; r <= m_warp->rows(); ++r) {
        for (int c = 0; c <= m_warp->cols(); ++c) {
            const double dist = QLineF(event->position(), handlePixelPos(c, r)).length();
            if (dist <= bestDist) { bestDist = dist; bestCol = c; bestRow = r; }
        }
    }
    if (bestCol >= 0) {
        m_dragging = true;
        m_dragCol = bestCol;
        m_dragRow = bestRow;
        update();
    }
}

void WarpCalibrationOverlay::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || !m_warp) return;
    // Consentito uscire un po' dal rettangolo [0,1] (margine extra) per
    // correggere proiettori che "sforano" oltre il bordo dello schermo o
    // per tirare un punto dentro oltre il centro in casi estremi. Il
    // trascinamento segue comunque la posizione VERA del mouse (non quella
    // inset dell'handle) così il punto arriva esattamente dove si rilascia.
    const double nx = qBound(-0.5, event->position().x() / qMax(1, width()),  1.5);
    const double ny = qBound(-0.5, event->position().y() / qMax(1, height()), 1.5);
    m_warp->setControlPoint(m_dragCol, m_dragRow, QPointF(nx, ny));
    update();
    emit warpEdited();
}

void WarpCalibrationOverlay::mouseReleaseEvent(QMouseEvent *) {
    if (!m_dragging) return;
    m_dragging = false;
    m_dragCol = m_dragRow = -1;
    update();
    emit warpCommitted();
}

void WarpCalibrationOverlay::addColumn() {
    if (!m_warp) return;
    m_warp->setGridSize(m_warp->cols() + 1, m_warp->rows());
    update();
    emit warpCommitted();
}

void WarpCalibrationOverlay::removeColumn() {
    if (!m_warp || m_warp->cols() <= 1) return;
    m_warp->setGridSize(m_warp->cols() - 1, m_warp->rows());
    update();
    emit warpCommitted();
}

void WarpCalibrationOverlay::addRow() {
    if (!m_warp) return;
    m_warp->setGridSize(m_warp->cols(), m_warp->rows() + 1);
    update();
    emit warpCommitted();
}

void WarpCalibrationOverlay::removeRow() {
    if (!m_warp || m_warp->rows() <= 1) return;
    m_warp->setGridSize(m_warp->cols(), m_warp->rows() - 1);
    update();
    emit warpCommitted();
}

void WarpCalibrationOverlay::resetWarp() {
    if (!m_warp) return;
    m_warp->reset();
    update();
    emit warpCommitted();
}
