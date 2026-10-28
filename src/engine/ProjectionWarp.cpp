#include "ProjectionWarp.h"
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QJsonArray>
#include <QFont>
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
    invalidateSmoothCache();
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
    invalidateSmoothCache();
}

QPointF ProjectionWarp::controlPoint(int col, int row) const {
    if (col < 0 || col > m_cols || row < 0 || row > m_rows) return {};
    return m_points[index(col, row)];
}

void ProjectionWarp::setControlPoint(int col, int row, QPointF normalizedPos) {
    if (col < 0 || col > m_cols || row < 0 || row > m_rows) return;
    m_points[index(col, row)] = normalizedPos;
    invalidateSmoothCache();
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
    if (windowSize.isEmpty() || source.isNull()) {
        QImage result(windowSize, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        return result;
    }

    // Vedi il commento in ProjectionWarp.h: il costo del warp cresce con la
    // risoluzione sia sorgente che destinazione (non col numero di celle),
    // e a 1920x1080 anche un warp semplice supera abbondantemente il budget
    // di un frame a 30fps. Si lavora quindi a una risoluzione ridotta (il
    // lato lungo non supera kMaxWorkingDim) e si scala il risultato alla
    // dimensione vera con un unico blit economico.
    constexpr int kMaxWorkingDim = 960;
    const int longSide = qMax(windowSize.width(), windowSize.height());
    const double scale = longSide > kMaxWorkingDim ? double(kMaxWorkingDim) / longSide : 1.0;

    if (scale >= 1.0)
        return renderWarpedAt(source, windowSize);

    const QSize workingSize(qMax(1, qRound(windowSize.width() * scale)),
                             qMax(1, qRound(windowSize.height() * scale)));
    const QImage workingSource = source.scaled(
        qMax(1, qRound(source.width() * scale)), qMax(1, qRound(source.height() * scale)),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QImage warped = renderWarpedAt(workingSource, workingSize);
    return warped.scaled(windowSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage ProjectionWarp::renderWarpedAt(const QImage &source, QSize windowSize) const {
    QImage result(windowSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    if (windowSize.isEmpty() || source.isNull()) return result;

    QPainter painter(&result);
    // L'antialiasing sul contorno del poligono di ciascuna cella è
    // indispensabile per un bordo esterno liscio (senza, il contorno
    // dell'intera area deformata appare "a scalini" — osservato). A piena
    // risoluzione il suo costo era proibitivo; ora che si lavora sulla
    // risoluzione interna ridotta (vedi renderWarped()) il margine di
    // prestazioni basta a riattivarlo senza uscire dal budget di un frame.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double w = windowSize.width();
    const double h = windowSize.height();

    // Il ritaglio di ogni cella è fatto sulla sorgente ALLA SUA RISOLUZIONE
    // REALE (non quella della finestra), altrimenti con windowSize diverso
    // dalla dimensione dell'immagine sorgente (es. un'immagine più grande
    // della finestra) i rettangoli sorgente calcolati in "spazio finestra"
    // non corrispondono ai pixel giusti dell'immagine.
    const double srcW = source.width();
    const double srcH = source.height();

    // Si usa la griglia DENSA (spline liscia), non quella grezza: più celle,
    // più piccole, ciascuna con una trasformazione quasi-affine — così la
    // deformazione complessiva segue una curva continua invece di una serie
    // di facce piatte visibilmente spezzate ai bordi tra celle.
    int denseCols = m_cols, denseRows = m_rows;
    const QVector<QPointF> &grid = smoothGrid(&denseCols, &denseRows);
    auto gridAt = [&](int col, int row) { return grid[row * (denseCols + 1) + col]; };

    for (int r = 0; r < denseRows; ++r) {
        for (int c = 0; c < denseCols; ++c) {
            // Rettangolo della cella in pixel sorgente (sempre assiale: è la
            // posizione "identità", il rettangolo che occuperebbe senza
            // alcun warp).
            const QRect cellRect(
                qRound(c       * srcW / denseCols), qRound(r       * srcH / denseRows),
                qRound((c + 1) * srcW / denseCols) - qRound(c * srcW / denseCols),
                qRound((r + 1) * srcH / denseRows) - qRound(r * srcH / denseRows));
            if (cellRect.isEmpty()) continue;

            // IMPORTANTE: si deforma solo il ritaglio della cella, non
            // l'immagine intera con un transform locale e poi clip — quello
            // faceva impazzire il campionamento del rasterizzatore su
            // trasformazioni estreme (celle molto strette/oblique),
            // producendo artefatti di stiramento/sbavatura su tutta
            // l'immagine invece di restare confinati alla cella.
            //
            // Il ritaglio include però un piccolo margine ("bleed") oltre il
            // vero bordo della cella: senza di esso, il campionamento
            // bilineare (SmoothPixmapTransform) ai pixel appena dentro il
            // bordo non ha pixel vicini validi da cui interpolare (il ritaglio
            // finisce lì), e sfuma verso il nero/trasparente — una sottile
            // frangia scura visibile esattamente al confine fra due celle,
            // indipendente dalla fessura del clip (già corretta sotto). Il
            // margine prende i pixel REALI della cella adiacente invece che
            // un bordo "clampato", eliminando anche quella frangia.
            constexpr int kBleedPx = 3;
            const QRect paddedRect = cellRect.adjusted(-kBleedPx, -kBleedPx, kBleedPx, kBleedPx)
                                              .intersected(source.rect());
            const QImage cellSrc = source.copy(paddedRect);
            // Dove cade il vero bordo della cella dentro il ritaglio con
            // margine (può essere meno di kBleedPx se il margine è stato
            // tagliato dal bordo dell'immagine sorgente).
            const double offX = cellRect.left() - paddedRect.left();
            const double offY = cellRect.top()  - paddedRect.top();

            QPolygonF srcQuad;
            srcQuad << QPointF(offX, offY)
                    << QPointF(offX + cellRect.width(), offY)
                    << QPointF(offX + cellRect.width(), offY + cellRect.height())
                    << QPointF(offX, offY + cellRect.height());

            auto dst = [&](int col, int row) {
                const QPointF p = gridAt(col, row);
                return QPointF(p.x() * w, p.y() * h);
            };
            QPolygonF dstQuad;
            dstQuad << dst(c, r) << dst(c + 1, r) << dst(c + 1, r + 1) << dst(c, r + 1);

            QTransform t;
            if (!QTransform::quadToQuad(srcQuad, dstQuad, t)) continue;

            // Il clip alla sola cella destinazione evita che celle vicine si
            // sovrappongano/bucano ai bordi per via degli arrotondamenti del
            // rasterizzatore sulla trasformazione prospettica per-cella —
            // MA un clip esatto (bordo a bordo) lascia filtrare righe nere
            // tra le celle, perché l'antialiasing ammorbidisce il bordo del
            // clip di ciascuna cella in modo indipendente e i due bordi
            // adiacenti non coincidono mai a un sub-pixel esatto. Si espande
            // leggermente il poligono di clip (non il quad di destinazione
            // usato dalla trasformazione, che resta preciso) verso l'esterno
            // dal proprio centro: le celle finiscono così per sovrapporsi di
            // una frazione di pixel invece di confinare esattamente,
            // eliminando la fessura senza introdurre doppioni visibili
            // (il contenuto nella zona di sovrapposizione è quasi identico
            // da entrambi i lati, essendo la stessa immagine).
            QPolygonF clipQuad = dstQuad;
            {
                QPointF centroid(0, 0);
                for (const QPointF &p : clipQuad) centroid += p;
                centroid /= clipQuad.size();
                for (QPointF &p : clipQuad) {
                    QPointF dir = p - centroid;
                    const double len = std::hypot(dir.x(), dir.y());
                    if (len > 1e-6) p += dir * (0.75 / len);
                }
            }

            painter.save();
            QPainterPath clipPath;
            clipPath.addPolygon(clipQuad);
            painter.setClipPath(clipPath);
            painter.setTransform(t, true);
            painter.drawImage(0, 0, cellSrc);
            painter.restore();
        }
    }
    return result;
}

// Interpolazione cubica di Catmull-Rom tra p1 e p2 (p0/p3 sono i vicini usati
// per calcolare la tangente); t in [0,1]. Passa esattamente per p1 (t=0) e
// p2 (t=1) con tangente continua rispetto ai segmenti vicini — a differenza
// dell'interpolazione lineare, non crea un cambio di pendenza brusco in p1/p2.
static QPointF catmullRom(const QPointF &p0, const QPointF &p1,
                           const QPointF &p2, const QPointF &p3, double t) {
    const double t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * p1)
        + (p2 - p0) * t
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
        + (3.0 * p1 - p0 - 3.0 * p2 + p3) * t3);
}

// Subdivide una sequenza di punti con una spline di Catmull-Rom "clampata"
// (i vicini mancanti ai due estremi sono il punto estremo stesso duplicato,
// così la curva non "sfora" oltre il primo/ultimo punto). Risultato:
// (pts.size()-1)*subdiv + 1 punti, passanti esattamente per quelli originali
// agli indici multipli di subdiv.
static QVector<QPointF> catmullRomSubdivide(const QVector<QPointF> &pts, int subdiv) {
    if (pts.size() <= 1 || subdiv <= 1) return pts;
    const int n = pts.size();
    QVector<QPointF> out;
    out.reserve((n - 1) * subdiv + 1);
    for (int i = 0; i < n - 1; ++i) {
        const QPointF &p1 = pts[i];
        const QPointF &p2 = pts[i + 1];
        const QPointF &p0 = (i == 0)     ? pts[0]     : pts[i - 1];
        const QPointF &p3 = (i + 2 < n)  ? pts[i + 2] : pts[n - 1];
        for (int s = 0; s < subdiv; ++s)
            out.append(catmullRom(p0, p1, p2, p3, double(s) / subdiv));
    }
    out.append(pts.last());
    return out;
}

void ProjectionWarp::rebuildSmoothCache() const {
    const int subdiv = smoothSubdiv();

    // Passo 1: per ciascuna riga grezza, subdividi lungo le colonne.
    QVector<QVector<QPointF>> rowsDense; // (m_rows+1) righe, ciascuna densa in colonna
    rowsDense.reserve(m_rows + 1);
    for (int r = 0; r <= m_rows; ++r) {
        QVector<QPointF> row;
        row.reserve(m_cols + 1);
        for (int c = 0; c <= m_cols; ++c) row.append(m_points[index(c, r)]);
        rowsDense.append(catmullRomSubdivide(row, subdiv));
    }
    const int denseColsCount = rowsDense.isEmpty() ? (m_cols + 1) : rowsDense.first().size();

    // Passo 2: per ciascuna colonna della griglia intermedia (già densa in
    // colonna), subdividi lungo le righe — risultato: superficie densa in
    // entrambe le direzioni (prodotto tensoriale di due spline 1D).
    QVector<QVector<QPointF>> colsDense; // denseColsCount colonne, ciascuna densa in riga
    colsDense.reserve(denseColsCount);
    for (int j = 0; j < denseColsCount; ++j) {
        QVector<QPointF> col;
        col.reserve(m_rows + 1);
        for (int r = 0; r <= m_rows; ++r) col.append(rowsDense[r][j]);
        colsDense.append(catmullRomSubdivide(col, subdiv));
    }
    const int denseRowsCount = colsDense.isEmpty() ? (m_rows + 1) : colsDense.first().size();

    QVector<QPointF> dense;
    dense.reserve(denseRowsCount * denseColsCount);
    for (int r = 0; r < denseRowsCount; ++r)
        for (int j = 0; j < denseColsCount; ++j)
            dense.append(colsDense[j][r]);

    m_smoothPoints = dense;
    m_smoothCols = denseColsCount - 1;
    m_smoothRows = denseRowsCount - 1;
    m_smoothDirty = false;
}

const QVector<QPointF> &ProjectionWarp::smoothGrid(int *outCols, int *outRows) const {
    if (m_smoothDirty) rebuildSmoothCache();
    if (outCols) *outCols = m_smoothCols;
    if (outRows) *outRows = m_smoothRows;
    return m_smoothPoints;
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
    invalidateSmoothCache();
}

QImage projectionTestPattern(QSize size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    if (size.isEmpty()) return img;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double w = size.width();
    const double h = size.height();

    // Griglia di riferimento, una linea ogni 10% in ciascuna direzione.
    QPen gridPen(QColor(0, 200, 255, 180));
    gridPen.setWidthF(1.0);
    p.setPen(gridPen);
    for (int i = 1; i < 10; ++i) {
        const double x = w * i / 10.0;
        const double y = h * i / 10.0;
        p.drawLine(QPointF(x, 0), QPointF(x, h));
        p.drawLine(QPointF(0, y), QPointF(w, y));
    }

    // Bordo e crocicchio centrale, più marcati della griglia.
    QPen framePen(QColor(255, 255, 255, 230));
    framePen.setWidthF(3.0);
    p.setPen(framePen);
    p.drawRect(QRectF(1.5, 1.5, w - 3.0, h - 3.0));
    p.drawLine(QPointF(w / 2, 0), QPointF(w / 2, h));
    p.drawLine(QPointF(0, h / 2), QPointF(w, h / 2));

    // Angoli colorati diversamente, utile per orientarsi a colpo d'occhio
    // quando l'immagine warpata è molto deformata (keystone forte, ecc.).
    const double cornerSize = qMin(w, h) * 0.06;
    auto drawCorner = [&](QPointF pos, const QColor &color) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pos, cornerSize, cornerSize);
    };
    drawCorner(QPointF(0, 0), QColor(255, 60, 60));     // TL rosso
    drawCorner(QPointF(w, 0), QColor(60, 255, 60));     // TR verde
    drawCorner(QPointF(w, h), QColor(255, 220, 0));     // BR giallo
    drawCorner(QPointF(0, h), QColor(60, 140, 255));    // BL blu

    QFont font = p.font();
    font.setPixelSize(qMax(12, int(qMin(w, h) * 0.03)));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, w, h), Qt::AlignCenter,
               QString("%1 x %2").arg(size.width()).arg(size.height()));

    return img;
}
