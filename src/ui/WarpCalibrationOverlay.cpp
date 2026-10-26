#include "WarpCalibrationOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QPushButton>
#include <QLineF>

WarpCalibrationOverlay::WarpCalibrationOverlay(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAutoFillBackground(false);

    auto makeBtn = [this](const QString &text) {
        auto *b = new QPushButton(text, this);
        b->setFixedSize(28, 28);
        b->setStyleSheet(
            "QPushButton { background: rgba(30,30,30,200); color: white; "
            "border: 1px solid #888; border-radius: 4px; font-weight: bold; } "
            "QPushButton:hover { background: rgba(60,60,60,220); }");
        b->hide();
        return b;
    };
    m_addColBtn    = makeBtn("+|");
    m_removeColBtn = makeBtn("-|");
    m_addRowBtn    = makeBtn("+—");
    m_removeRowBtn = makeBtn("-—");
    m_resetBtn     = makeBtn("R");
    m_resetBtn->setToolTip(tr("Reset calibrazione"));
    m_addColBtn->setToolTip(tr("Aggiungi colonna alla griglia"));
    m_removeColBtn->setToolTip(tr("Rimuovi colonna dalla griglia"));
    m_addRowBtn->setToolTip(tr("Aggiungi riga alla griglia"));
    m_removeRowBtn->setToolTip(tr("Rimuovi riga dalla griglia"));

    connect(m_addColBtn,    &QPushButton::clicked, this, &WarpCalibrationOverlay::addColumn);
    connect(m_removeColBtn, &QPushButton::clicked, this, &WarpCalibrationOverlay::removeColumn);
    connect(m_addRowBtn,    &QPushButton::clicked, this, &WarpCalibrationOverlay::addRow);
    connect(m_removeRowBtn, &QPushButton::clicked, this, &WarpCalibrationOverlay::removeRow);
    connect(m_resetBtn,     &QPushButton::clicked, this, &WarpCalibrationOverlay::resetWarp);
}

void WarpCalibrationOverlay::setWarp(ProjectionWarp *warp) {
    m_warp = warp;
    update();
}

void WarpCalibrationOverlay::setCalibrating(bool on) {
    if (m_calibrating == on) return;
    m_calibrating = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    for (QPushButton *b : {m_addColBtn, m_removeColBtn, m_addRowBtn, m_removeRowBtn, m_resetBtn})
        b->setVisible(on);
    m_dragging = false;
    update();
}

void WarpCalibrationOverlay::layoutButtons() {
    const int margin = 8;
    int x = margin;
    const int y = margin;
    for (QPushButton *b : {m_addColBtn, m_removeColBtn, m_addRowBtn, m_removeRowBtn, m_resetBtn}) {
        b->move(x, y);
        x += b->width() + 4;
    }
}

void WarpCalibrationOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    layoutButtons();
}

void WarpCalibrationOverlay::paintEvent(QPaintEvent *) {
    if (!m_calibrating || !m_warp) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int cols = m_warp->cols();
    const int rows = m_warp->rows();
    const double w = width();
    const double h = height();

    auto toPx = [&](int c, int r) {
        const QPointF n = m_warp->controlPoint(c, r);
        return QPointF(n.x() * w, n.y() * h);
    };

    // Linee della mesh: più leggibile dei soli punti per capire come si sta
    // deformando la griglia mentre si trascina.
    QPen linePen(QColor(255, 210, 0, 200));
    linePen.setWidthF(1.5);
    p.setPen(linePen);
    for (int r = 0; r <= rows; ++r)
        for (int c = 0; c < cols; ++c)
            p.drawLine(toPx(c, r), toPx(c + 1, r));
    for (int c = 0; c <= cols; ++c)
        for (int r = 0; r < rows; ++r)
            p.drawLine(toPx(c, r), toPx(c, r + 1));

    // Handle ai punti di controllo.
    for (int r = 0; r <= rows; ++r) {
        for (int c = 0; c <= cols; ++c) {
            const QPointF pos = toPx(c, r);
            const bool active = (m_dragging && c == m_dragCol && r == m_dragRow);
            p.setBrush(active ? QColor(255, 255, 255) : QColor(255, 210, 0));
            p.setPen(QPen(Qt::black, 1.5));
            const double radius = active ? 7.0 : 5.0;
            p.drawEllipse(pos, radius, radius);
        }
    }
}

void WarpCalibrationOverlay::mousePressEvent(QMouseEvent *event) {
    if (!m_calibrating || !m_warp) return;
    int col = -1, row = -1;
    if (m_warp->nearestControlPoint(event->position(), size(), kHandleRadiusPx, &col, &row)) {
        m_dragging = true;
        m_dragCol = col;
        m_dragRow = row;
        update();
    }
}

void WarpCalibrationOverlay::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || !m_warp) return;
    // Consentito uscire un po' dal rettangolo [0,1] (margine extra) per
    // correggere proiettori che "sforano" oltre il bordo dello schermo o
    // per tirare un punto dentro oltre il centro in casi estremi.
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
