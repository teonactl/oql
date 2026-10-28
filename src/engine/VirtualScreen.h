#pragma once
#include "ProjectionWarp.h"
#include <QString>
#include <QRectF>
#include <QSize>
#include <QPolygonF>
#include <QJsonObject>
#include <QImage>

class QPainter;

// Un output fisico: una destinazione di output reale (un proiettore/monitor,
// selezionato per nome) con la propria calibrazione/correzione keystone —
// un solo ProjectionWarp per proiettore, perché la distorsione geometrica
// dipende dalla posizione/distanza fisica del proiettore stesso, non da
// come viene suddivisa la sua area in schermi virtuali (vedi VirtualScreen
// sotto). Le finestre di output (una per PhysicalOutput) vengono
// posizionate sullo QScreen corrispondente a physicalScreenName.
class PhysicalOutput {
public:
    PhysicalOutput();
    explicit PhysicalOutput(QString id);

    QString id()   const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    QString physicalScreenName() const { return m_physicalScreenName; }
    void setPhysicalScreenName(const QString &name) { m_physicalScreenName = name; }

    // Risoluzione del canvas di questo output. Automatica (default): segue
    // la geometria reale dello QScreen assegnato (rilevata a runtime, non
    // salvata). Manuale: una risoluzione esplicita, utile quando il
    // proiettore non è riconosciuto come schermo desktop separato o si
    // vuole renderizzare a una risoluzione diversa da quella nativa.
    bool  autoResolution() const { return m_autoResolution; }
    QSize resolution()     const { return m_resolution; }
    void setAutoResolution(bool v)   { m_autoResolution = v; }
    void setResolution(QSize s)      { m_resolution = s; }

    ProjectionWarp&       warp()       { return m_warp; }
    const ProjectionWarp& warp() const { return m_warp; }
    void setWarp(const ProjectionWarp &warp) { m_warp = warp; }

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

private:
    QString m_id;
    QString m_name;
    QString m_physicalScreenName;
    bool    m_autoResolution = true;
    QSize   m_resolution     = QSize(1920, 1080);
    ProjectionWarp m_warp;
};

// Uno schermo virtuale: una forma arbitraria (poligono, non solo un
// rettangolo) ritagliata dentro l'area di UN PhysicalOutput — più schermi
// virtuali possono condividere lo stesso output fisico (es. un unico
// proiettore diviso in 3 zone di forma qualsiasi). Le cue video/immagine si
// assegnano a schermi virtuali, non direttamente agli output fisici.
//
// canvasRect indica quale porzione normalizzata [0,1]x[0,1] del contenuto
// di una cue questo schermo mostra: lo stesso meccanismo copre sia il
// mirror (canvasRect = tutto, il default) sia lo split in stile video-wall
// (canvasRect ritagliato diversamente per ciascuno schermo virtuale), senza
// bisogno di due modalità distinte.
class VirtualScreen {
public:
    // Come il contenuto (video/immagine, già ritagliato da canvasRect) si
    // posiziona dentro l'area di questo schermo (il bounding box del suo
    // maskPolygon), invece di essere sempre stirato per riempirla:
    //   Stretch: riempie esattamente l'area, deformando le proporzioni
    //            (comportamento di prima, utile solo se le proporzioni
    //            combaciano già).
    //   Contain: scala mantenendo le proporzioni per stare TUTTO dentro
    //            l'area ("adatta"/fit) — può lasciare bande vuote.
    //   Cover:   scala mantenendo le proporzioni per RIEMPIRE tutta l'area
    //            ("riempi") — può tagliare il contenuto in eccesso.
    //   Center:  nessuna scala, dimensione nativa del contenuto ritagliato.
    //   Manual:  come Contain ma con uno zoom (>=1) impostabile a mano —
    //            l'unico modo che garantisce SEMPRE un margine di
    //            trascinamento apprezzabile (con Contain/Cover il margine
    //            dipende dalle proporzioni del contenuto e può essere
    //            minimo o nullo per coincidenza), utile per esempio a
    //            ritagliare via le bande nere di un contenuto con
    //            proporzioni diverse dalla zona scegliendo a mano quale
    //            porzione restare visibile.
    // contentAnchor (default 0.5,0.5 = centrato) decide DOVE, per Contain/
    // Cover/Center/Manual: (0,0) = in alto a sinistra, (1,1) = in basso a
    // destra, valori intermedi per una posizione specifica qualsiasi.
    enum class FitMode { Stretch, Contain, Cover, Center, Manual };

    VirtualScreen();
    explicit VirtualScreen(QString id);

    QString id()   const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    QString physicalOutputId() const { return m_physicalOutputId; }
    void setPhysicalOutputId(const QString &id) { m_physicalOutputId = id; }

    // Poligono in coordinate normalizzate [0,1]x[0,1] relative al canvas
    // dell'output fisico a cui appartiene questo schermo. Default: l'intero
    // rettangolo [0,1]x[0,1] (equivalente a "tutto l'output"), ma può
    // essere un poligono qualsiasi disegnato dall'utente.
    QPolygonF maskPolygon() const { return m_maskPolygon; }
    void setMaskPolygon(const QPolygonF &polygon) { m_maskPolygon = polygon; }

    QRectF canvasRect() const { return m_canvasRect; }
    void setCanvasRect(QRectF rect) { m_canvasRect = rect; }

    FitMode fitMode() const { return m_fitMode; }
    void setFitMode(FitMode m) { m_fitMode = m; }

    QPointF contentAnchor() const { return m_contentAnchor; }
    void setContentAnchor(QPointF a) { m_contentAnchor = a; }

    // Usato solo da FitMode::Manual (ignorato dagli altri modi) — 1.0 =
    // come Contain, valori maggiori ritagliano progressivamente più
    // contenuto per lasciare spazio al trascinamento.
    double zoom() const { return m_zoom; }
    void setZoom(double z) { m_zoom = qBound(1.0, z, 8.0); }

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &o);

private:
    QString m_id; // stabile, usato per riferirlo dalle cue — non il nome (può cambiare)
    QString m_name;
    QString m_physicalOutputId;
    QPolygonF m_maskPolygon = QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0));
    QRectF    m_canvasRect  = QRectF(0.0, 0.0, 1.0, 1.0);
    FitMode   m_fitMode     = FitMode::Contain;
    QPointF   m_contentAnchor = QPointF(0.5, 0.5);
    double    m_zoom = 1.0;
};

// Applica a `fullFrame` (il contenuto già composto, della stessa dimensione
// del canvas del PhysicalOutput) il ritaglio canvasRect e la maschera
// maskPolygon di `screen`: il risultato (stessa dimensione di fullFrame) è
// la porzione di contenuto che QUESTO schermo virtuale deve mostrare, con
// tutto ciò che è fuori dal poligono lasciato trasparente. Il warp del
// PhysicalOutput va applicato DOPO il risultato di questa funzione (corregge
// la geometria dell'intero canvas, non del singolo schermo virtuale).
// `content`: l'immagine nativa della cue (qualunque sia la sua risoluzione —
// NON già adattata/letterboxata a canvasSize, altrimenti fitMode lavorerebbe
// su bande nere già "cotte" nei pixel invece che sul contenuto vero).
// `canvasSize`: dimensione del canvas dell'output fisico — determina dove si
// trova in pixel il poligono/target di `screen`. Il risultato ha sempre
// dimensione canvasSize.
//
// Versione che disegna direttamente su un QPainter già pronto (il canvas
// condiviso di un output con più schermi) invece di restituire una nuova
// QImage: usata dal percorso "caldo" (un repaint video/immagine, fino a 30
// volte al secondo) per evitare di allocare+riempire+ricopiare un buffer
// trasparente a parte per OGNI schermo a OGNI frame — con più schermi quel
// costo si moltiplicava ed era una causa concreta di scatti nella
// riproduzione. Fuori dall'area dello schermo non disegna nulla: resta
// quello che c'era già nel canvas condiviso (tipicamente nero), visivamente
// identico al comportamento precedente (un'area trasparente compositata su
// un canvas nero dà comunque nero).
void applyVirtualScreenMask(QPainter &painter, const QImage &content, QSize canvasSize, const VirtualScreen &screen);

// Versione "di comodo" che alloca e restituisce una nuova QImage — usata da
// anteprime/test dove non c'è già un QPainter condiviso a disposizione.
QImage applyVirtualScreenMask(const QImage &content, QSize canvasSize, const VirtualScreen &screen);

// Bounding box (in pixel, dato un canvas di dimensione canvasSize) del
// maskPolygon di `screen` — l'intero canvas se il poligono è degenere
// (meno di 3 punti o area nulla). Estratta da applyVirtualScreenMask così
// un'anteprima (es. il drag dell'anchor in ScreenFitDialog) può conoscere
// la stessa area di destinazione senza duplicarne il calcolo.
QRectF virtualScreenTargetRect(const VirtualScreen &screen, QSize canvasSize);

// Rettangolo (in pixel) in cui un contenuto di dimensione `contentSize`
// viene disegnato dentro `targetPx` secondo `mode`/`anchor` — la stessa
// geometria che applyVirtualScreenMask usa per il passo 3 (fitMode),
// esposta a parte così un'anteprima puà calcolare lo "spazio libero"
// disponibile per tradurre un trascinamento del mouse in un nuovo anchor.
// `zoom` è usato solo da FitMode::Manual (ignorato dagli altri modi) —
// default 1.0 per compatibilità con le chiamate che non lo passano.
QRectF fitDestRect(QSizeF contentSize, QRectF targetPx, VirtualScreen::FitMode mode, QPointF anchor, double zoom = 1.0);
