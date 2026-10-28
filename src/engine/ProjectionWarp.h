#pragma once
#include <QVector>
#include <QPointF>
#include <QSize>
#include <QImage>
#include <QJsonObject>
#include <cmath>

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
    //
    // Misurato: il rasterizzatore software di Qt non ha una scorciatoia
    // veloce per un blit sotto trasformazione prospettica (quella usata per
    // ogni cella) — il costo cresce con la risoluzione sia sorgente che
    // destinazione, non con il numero di celle. A 1920x1080 anche un warp
    // semplice supera abbondantemente i 33ms di budget di un frame video a
    // 30fps (causa concreta di scatti nella riproduzione). Per questo si
    // deforma internamente a una risoluzione ridotta (vedi kMaxWorkingDim
    // nel .cpp) e si scala il risultato alla dimensione vera con un unico
    // blit economico — una lieve perdita di nitidezza, impercettibile su un
    // contenuto proiettato su una superficie fisica, in cambio di una
    // riproduzione fluida.
    QImage renderWarped(const QImage &source, QSize windowSize) const;

    // Griglia "densa" liscia, ottenuta interpolando i punti di controllo con
    // una spline di Catmull-Rom (bidirezionale, a prodotto tensoriale) invece
    // che collegandoli con rette. Una trasformazione proiettiva per-cella
    // mappa rette in rette: usando direttamente la griglia grezza, al bordo
    // tra due celle adiacenti la pendenza cambia bruscamente e la superficie
    // sembra "spezzata" a sfaccettature invece che curva — visibile
    // chiaramente quando si deforma per adattarsi a una superficie non
    // piatta. Questa griglia densa è quella che renderWarped() e l'overlay
    // di calibrazione usano davvero per disegnare/deformare; i punti di
    // controllo "veri" restano quelli grezzi (sono quelli che l'utente
    // trascina). Risultato cache-ato, ricalcolato solo quando la griglia
    // grezza cambia (vedi invalidateSmoothCache()).
    const QVector<QPointF> &smoothGrid(int *outCols, int *outRows) const;
    // Adattiva invece di un valore fisso: renderWarped() ricalcola TUTTE le
    // celle dense a ogni singolo frame video (il contenuto sorgente cambia,
    // la cache della griglia da sola non basta) — con un fattore fisso
    // abbastanza alto da rendere lisci i bordi su una griglia 1x1/2x2
    // (serve ~5 lì), una calibrazione più fitta (es. 6x6) avrebbe generato
    // migliaia di celle dense per frame, abbastanza da far scattare audio e
    // video. La formula sceglie un fattore via via più basso man mano che
    // la griglia grezza è già fitta di suo (ha già abbastanza punti veri da
    // non aver bisogno di tanta sotto-suddivisione), tenendo il totale di
    // celle dense intorno a un budget costante indipendentemente da quanto
    // fine è la griglia configurata.
    int smoothSubdiv() const {
        constexpr int kTargetDenseCells = 360;
        const int coarseCells = qMax(1, m_cols * m_rows);
        return qBound(2, int(std::sqrt(double(kTargetDenseCells) / coarseCells)), 6);
    }

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

private:
    int index(int col, int row) const { return row * (m_cols + 1) + col; }
    void invalidateSmoothCache() const { m_smoothDirty = true; }
    void rebuildSmoothCache() const;
    // La vecchia implementazione di renderWarped(), invariata: deforma
    // esattamente a source.size()/windowSize senza alcun ridimensionamento
    // interno — richiamata da renderWarped() sulla risoluzione di lavoro
    // (ridotta o reale a seconda della dimensione richiesta).
    QImage renderWarpedAt(const QImage &source, QSize windowSize) const;

    int m_cols = 1;
    int m_rows = 1;
    QVector<QPointF> m_points; // (cols+1)*(rows+1) punti, riga per riga

    mutable bool m_smoothDirty = true;
    mutable QVector<QPointF> m_smoothPoints;
    mutable int m_smoothCols = 0;
    mutable int m_smoothRows = 0;
};

// Pattern di riferimento (griglia + crocicchio centrale + etichette agli
// angoli) da proiettare durante la calibrazione: senza un contenuto video/
// immagine attivo non c'è nulla su cui allineare visivamente lo schermo
// fisico, quindi le finestre di output lo mostrano al posto del contenuto
// normale finché la calibrazione è attiva.
QImage projectionTestPattern(QSize size);
