#include "ProjectionWarp.h"
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QJsonArray>
#include <cmath>

ProjectionWarp::ProjectionWarp() {
    reset();
}

static QPointF identityPoint(int col, int row, int cols, int rows) {
    return QPointF(cols > 0 ? double(col) / cols : 0.0,
                    rows > 0 ? double(row) / rows : 0.0);
}

// Campiona la griglia corrente in un punto arbitrario (u,v) in [0,1]x[0,1]
// con interpolazione bilineare tra le celle — usato per ricampionare la
// griglia quando cambia la risoluzione (setGridSize), così una calibrazione
// esistente non viene persa/azzerata ma solo ridistribuita sui nuovi punti.
static QPointF bilinearSample(const QVector<QPointF> &points, int cols, int rows, double u, double v) {
    const double fx = qBound(0.0, u, 1.0) * cols;
    const double fy = qBound(0.0, v, 1.0) * rows;
    int c0 = qBound(0, int(std::floor(fx)), cols);
    int r0 = qBound(0, int(std::floor(fy)), rows);
    int c1 = qMin(c0 + 1, cols);
    int r1 = qMin(r0 + 1, rows);
    const double tx = fx - c0;
    const double ty = fy - r0;

    auto at = [&](int c, int r) { return points[r * (cols + 1) + c]; };
    const QPointF p00 = at(c0, r0), p10 = at(c1, r0), p01 = at(c0, r1), p11 = at(c1, r1);
    const QPointF top    = p00 * (1.0 - tx) + p10 * tx;
    const QPointF bottom = p01 * (1.0 - tx) + p11 * tx;
    return top * (1.0 - ty) + bottom * ty;
}

void ProjectionWarp::setGridSize(int cols, int rows) {
    cols = qMax(1, cols);
    rows = qMax(1, rows);
    if (cols == m_cols && rows == m_rows) return;

    QVector<QPointF> newPoints;
    newPoints.reserve((cols + 1) * (rows + 1));
    for (int r = 0; r <= rows; ++r) {
        for (int c = 0; c <= cols; ++c) {
            const double u = double(c) / cols;
            const double v = double(r) / rows;
            newPoints.append(bilinearSample(m_points, m_cols, m_rows, u, v));
        }
    }
    m_cols = cols;
    m_rows = rows;
    m_points = newPoints;
}

bool ProjectionWarp::isIdentity() const {
    for (int r = 0; r <= m_rows; ++r) {
        for (int c = 0; c <= m_cols; ++c) {
            if (m_points[index(c, r)] != identityPoint(c, r, m_cols, m_rows))
                return false;
        }
    }
    return true;
}

void ProjectionWarp::reset() {
    m_points.clear();
    m_points.reserve((m_cols + 1) * (m_rows + 1));
    for (int r = 0; r <= m_rows; ++r)
        for (int c = 0; c <= m_cols; ++c)
            m_points.append(identityPoint(c, r, m_cols, m_rows));
}

QPointF ProjectionWarp::controlPoint(int col, int row) const {
    if (col < 0 || col > m_cols || row < 0 || row > m_rows) return {};
    return m_points[index(col, row)];
}

void ProjectionWarp::setControlPoint(int col, int row, QPointF normalizedPos) {
    if (col < 0 || col > m_cols || row < 0 || row > m_rows) return;
    m_points[index(col, row)] = normalizedPos;
}

bool ProjectionWarp::nearestControlPoint(QPointF posPx, QSize windowSize, double radiusPx,
                                          int *outCol, int *outRow) const {
    double bestDist = radiusPx;
    bool found = false;
    for (int r = 0; r <= m_rows; ++r) {
        for (int c = 0; c <= m_cols; ++c) {
            const QPointF p = m_points[index(c, r)];
            const QPointF pxPos(p.x() * windowSize.width(), p.y() * windowSize.height());
            const double dist = QLineF(posPx, pxPos).length();
            if (dist <= bestDist) {
                bestDist = dist;
                if (outCol) *outCol = c;
                if (outRow) *outRow = r;
                found = true;
            }
        }
    }
    return found;
}

QImage ProjectionWarp::renderWarped(const QImage &source, QSize windowSize) const {
    QImage result(windowSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    if (windowSize.isEmpty() || source.isNull()) return result;

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double w = windowSize.width();
    const double h = windowSize.height();

    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            // La cella sorgente è sempre la sua posizione "identità" (il
            // rettangolo che occuperebbe senza alcun warp) — è la
            // destinazione, presa dai punti di controllo correnti, che
            // determina la deformazione.
            const QPointF srcTL(c       * w / m_cols, r       * h / m_rows);
            const QPointF srcTR((c + 1) * w / m_cols, r       * h / m_rows);
            const QPointF srcBR((c + 1) * w / m_cols, (r + 1) * h / m_rows);
            const QPointF srcBL(c       * w / m_cols, (r + 1) * h / m_rows);
            QPolygonF srcQuad; srcQuad << srcTL << srcTR << srcBR << srcBL;

            auto dst = [&](int col, int row) {
                const QPointF p = controlPoint(col, row);
                return QPointF(p.x() * w, p.y() * h);
            };
            QPolygonF dstQuad;
            dstQuad << dst(c, r) << dst(c + 1, r) << dst(c + 1, r + 1) << dst(c, r + 1);

            QTransform t;
            if (!QTransform::quadToQuad(srcQuad, dstQuad, t)) continue;

            painter.save();
            // Il clip alla sola cella destinazione evita che celle vicine si
            // sovrappongano/bucano ai bordi per via degli arrotondamenti del
            // rasterizzatore sulla trasformazione prospettica per-cella.
            QPainterPath clipPath;
            clipPath.addPolygon(dstQuad);
            painter.setClipPath(clipPath);
            painter.setTransform(t, true);
            painter.drawImage(0, 0, source);
            painter.restore();
        }
    }
    return result;
}

QJsonObject ProjectionWarp::toJson() const {
    QJsonObject o;
    o["cols"] = m_cols;
    o["rows"] = m_rows;
    QJsonArray pts;
    for (const QPointF &p : m_points) {
        pts.append(p.x());
        pts.append(p.y());
    }
    o["points"] = pts;
    return o;
}

void ProjectionWarp::fromJson(const QJsonObject &o) {
    const int cols = qMax(1, o["cols"].toInt(1));
    const int rows = qMax(1, o["rows"].toInt(1));
    const QJsonArray pts = o["points"].toArray();
    const int expected = (cols + 1) * (rows + 1) * 2;
    if (pts.size() != expected) {
        // JSON mancante/corrotto: resta sulla griglia identità alla
        // risoluzione richiesta invece di lasciare lo stato precedente
        // (che potrebbe essere di dimensione diversa e disallineare index()).
        m_cols = cols;
        m_rows = rows;
        reset();
        return;
    }
    m_cols = cols;
    m_rows = rows;
    m_points.clear();
    m_points.reserve((cols + 1) * (rows + 1));
    for (int i = 0; i < pts.size(); i += 2)
        m_points.append(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
}
