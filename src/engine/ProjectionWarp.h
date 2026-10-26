#pragma once
#include <QVector>
#include <QPointF>
#include <QSize>
#include <QImage>
#include <QJsonObject>

// Calibrazione dell'area di proiezione: una griglia di punti di controllo
// (cols+1) x (rows+1) in coordinate normalizzate [0,1] relative alla
// finestra di output. La griglia identità (nessuna deformazione) ha i punti
// equispaziati nel rettangolo [0,1]x[0,1]. Muovendo i punti si ottiene un
// warp per-cella (ogni cella è un quadrilatero deformato indipendentemente
// via QTransform::quadToQuad), che approssima un vero mesh-warp: con una
// griglia sufficientemente fine corregge non solo il keystone ma anche
// schermi curvi o irregolari — a differenza di un semplice warp a 4 angoli
// che è solo un caso degenere con cols=rows=1.
class ProjectionWarp {
public:
    ProjectionWarp();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    // Ridimensiona la griglia a cols x rows celle, mantenendo per quanto
    // possibile la forma attuale (i punti esistenti vengono ricampionati
    // bilinearmente sulla nuova risoluzione, non resettati a identità).
    void setGridSize(int cols, int rows);

    bool isIdentity() const;
    void reset(); // torna alla griglia identità (nessun warp) mantenendo cols/rows

    // Punto di controllo (col,row) in [0..cols]x[0..rows], coordinate
    // normalizzate [0,1] relative alla finestra di destinazione.
    QPointF controlPoint(int col, int row) const;
    void    setControlPoint(int col, int row, QPointF normalizedPos);

    // Punto di controllo più vicino a `pos` (pixel, spazio finestra di
    // dimensione windowSize), utile per l'overlay di calibrazione quando
    // l'utente clicca per afferrare un handle. radiusPx limita la distanza
    // massima; ritorna false se nessun punto è entro quel raggio.
    bool nearestControlPoint(QPointF posPx, QSize windowSize, double radiusPx,
                              int *outCol, int *outRow) const;

    // Deforma `source` (immagine già scalata/posizionata com'è mostrata
    // senza warp, cioè a piena dimensione windowSize) producendo una nuova
    // immagine di dimensione windowSize con il warp applicato. Le zone della
    // destinazione fuori da ogni cella (quando i punti si "chiudono" sul
    // bordo) restano trasparenti.
    QImage renderWarped(const QImage &source, QSize windowSize) const;

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

private:
    int index(int col, int row) const { return row * (m_cols + 1) + col; }

    int m_cols = 1;
    int m_rows = 1;
    QVector<QPointF> m_points; // (cols+1)*(rows+1) punti, riga per riga
};
