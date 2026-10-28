#pragma once
#include <QWidget>
#include <QString>
#include <functional>

class Workspace;
class VirtualScreen;

// Anteprima trascinabile di come il contenuto di una cue riempie l'area di
// UNO VirtualScreen, usata da ScreenFitDialog. Non ricompone da sé il
// contenuto: riceve un "content provider" (la cue stessa, tramite
// InspectorPanel/ScreenFitDialog, sa come produrre un'immagine nativa
// rappresentativa — frame corrente per un video, immagine/colore per una
// immagine) e applica la STESSA applyVirtualScreenMask()/fitDestRect() usata
// dalla resa reale, così l'anteprima non può disallinearsi da essa.
//
// Trascinare col mouse dentro l'anteprima sposta il contentAnchor dello
// VirtualScreen (persistito live, come il resto degli editor schermi).
class ScreenFitPreview : public QWidget {
    Q_OBJECT
public:
    explicit ScreenFitPreview(QWidget *parent = nullptr);

    // contentProvider: funzione che restituisce l'immagine nativa da
    // mostrare (dimensione qualsiasi) per l'anteprima corrente.
    void setTarget(Workspace *workspace, const QString &screenId,
                   std::function<QImage()> contentProvider);

    // Da richiamare quando fitMode/canvasRect cambiano altrove (es. dai
    // controlli numerici nello stesso dialogo) per aggiornare subito
    // l'anteprima senza aspettare un evento esterno.
    void refresh();

signals:
    // Emesso a trascinamento concluso (persistere/notificare il workspace
    // spetta al chiamante, coerente con gli altri editor schermi).
    void anchorEdited();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRectF canvasPixelRect() const; // area nelle proporzioni reali dell'output, dentro il widget

    // targetPx (area di destinazione) e destRect (dove finisce davvero il
    // contenuto, frame vero se disponibile altrimenti il segnaposto) nelle
    // coordinate LOCALI di canvasPixelRect() — stessa logica di
    // applyVirtualScreenMask/fitDestRect, condivisa tra paintEvent (per il
    // suggerimento "nessun margine da trascinare") e mousePressEvent (per il
    // trascinamento), così restano sempre coerenti tra loro.
    struct FitGeometry { QRectF targetPx; QRectF destRect; };
    FitGeometry currentFitGeometry(const QRectF &canvasRect, VirtualScreen *vs) const;

    // Dimensione usata per il riquadro segnaposto (e per il relativo
    // trascinamento) quando non c'è ancora un frame vero disponibile — non
    // influisce sul contenuto reale, serve solo a dare un feedback visivo
    // utile prima che la cue venga avviata. Deliberatamente NON 16:9: se
    // combaciasse con le proporzioni tipiche di uno schermo/zona, Adatta e
    // Riempi avrebbero margine di trascinamento ~zero su entrambi gli assi
    // (comportamento corretto quando le proporzioni combaciano davvero, ma
    // fuorviante come segnaposto prima che la cue sia avviata) — un quadrato
    // garantisce quasi sempre bande visibili e quindi margine da trascinare.
    static constexpr QSize kPlaceholderContentSize = QSize(1200, 1200);

    Workspace *m_workspace = nullptr;
    QString    m_screenId;
    std::function<QImage()> m_contentProvider;

    bool   m_dragging = false;
    QPointF m_dragStartAnchor;
    QPoint  m_dragStartMouse;
    // La sensibilità del trascinamento è legata alla dimensione della ZONA
    // (sempre positiva e stabile), non al margine effettivo residuo: con
    // Manuale, più si aumenta lo zoom più il margine reale cresce, e usarlo
    // come denominatore renderebbe il trascinamento via via meno sensibile
    // fino a sembrare senza effetto (bug osservato: "posso zoomare ma non
    // spostare"). m_dragRange è quindi sempre la dimensione della zona;
    // m_dragSign (+1/-1 per asse) codifica solo la direzione corretta,
    // diversa secondo che il contenuto sia più piccolo (Adatta: trascinare a
    // destra lo sposta a destra) o più grande (Riempi/zoom: trascinare a
    // destra rivela la parte destra del contenuto spostandolo a sinistra)
    // dell'area di destinazione su quell'asse. Zero quando quell'asse non ha
    // davvero nessun grado di libertà (Stirato, o un caso limite in cui le
    // proporzioni combaciano esattamente).
    QSizeF  m_dragRange;
    QPointF m_dragSign;
};
