#pragma once
#include "engine/VirtualScreen.h"
#include <QWidget>
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <QMetaObject>

class Workspace;
class VideoCue;

// Finestra di output video — non è più legata a UNA cue tramite un unico
// QVideoSink: ogni VideoCue possiede ormai il proprio sink e il proprio
// ultimo frame (VideoCue::currentFrame(), pull invece di push — vedi
// VideoCue.h). Questa finestra, a ogni ridisegno, scorre tutti gli
// VirtualScreen del suo PhysicalOutput e per ciascuno chiede alla CueList
// quale VideoCue è assegnata e in riproduzione lì (CueList::
// activeVideoCueForScreen), ne compone il contenuto (sfondo, frame, fade —
// proprietà lette dalla cue stessa, non più spinte dall'esterno), lo
// ritaglia/maschera con applyVirtualScreenMask() e lo disegna sul canvas
// condiviso. Il warp del PhysicalOutput si applica una sola volta, alla
// fine, sull'intero canvas.
class VideoOutputWindow : public QWidget {
    Q_OBJECT
public:
    // embedded=true: niente Qt::Window/titolo/resize iniziale, e il canvas
    // riparte da trasparente invece che nero — usata quando questa finestra
    // vive come layer dentro CombinedOutputWindow invece che come finestra
    // separata (vedi quel file).
    explicit VideoOutputWindow(QWidget *parent = nullptr, bool embedded = false);

    // Workspace e id del PhysicalOutput che questa finestra rappresenta —
    // non posseduti. Per ora esiste sempre e solo UNA finestra video,
    // legata al primo output (vero multi-finestra dinamico resta da fare,
    // vedi il commento in MainWindow.cpp dove viene chiamato).
    void setOutput(Workspace *workspace, const QString &physicalOutputId);

    void toggleFullScreen();

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event)        override;
    void mouseMoveEvent(QMouseEvent *event)         override;
    void mouseReleaseEvent(QMouseEvent *event)      override;
    void keyPressEvent(QKeyEvent *event)            override;
    void showEvent(QShowEvent *event)               override;
    void hideEvent(QHideEvent *event)               override;

private:
    QImage composeCueFrame(VideoCue *cue) const;
    QImage composeCanvas() const;
    // Ricollega update() al frameChanged() di ciascuna VideoCue attualmente
    // attiva su uno degli schermi di questo output — vedi il commento su
    // m_cueFrameConnections sul perché, oltre al timer.
    void rescanActiveCueConnections();

    Workspace *m_workspace = nullptr;
    QString    m_outputId;

    // Ridisegno periodico come RETE DI SICUREZZA (cambio di stato, warp
    // trascinato, primo frame prima che arrivi un segnale, ecc.) — non più
    // l'unico motore del ridisegno: un timer a frequenza fissa (30fps) e il
    // framerate reale del video sono due orologi indipendenti che non
    // combaciano mai esattamente, e la discrepanza produce scatti/salti
    // percepibili (frame mostrati due volte o saltati) anche quando il
    // costo per frame è basso. Vedi rescanActiveCueConnections() per il
    // ridisegno vero, agganciato all'arrivo di ogni frame reale.
    QTimer *m_repaintTimer;

    // Una connessione per ciascuna VideoCue attiva su uno degli schermi di
    // questo output: ogni frameChanged() reale chiama update() subito,
    // invece di aspettare il prossimo tick del timer — elimina la
    // desincronizzazione fra il timer e il framerate vero del video.
    // Ricostruite a ogni cambio di stato/proprietà delle cue (vedi
    // setOutput()) perché quale cue è "attiva" per ciascuno schermo può
    // cambiare in qualunque momento.
    QList<QMetaObject::Connection> m_cueFrameConnections;

    mutable QHash<QString, QPixmap> m_pixmapCache; // sfondo-immagine per cue, per path
    bool m_embedded = false;

    // Trascinamento manuale della finestra (senza bordi/decorazioni, vedi
    // Qt::FramelessWindowHint — niente barra del titolo da cui spostarla
    // altrimenti). window() invece di this: da incorporata, sposta la
    // finestra top-level che la contiene (CombinedOutputWindow), non se
    // stessa (che è solo un suo layer).
    bool   m_draggingWindow = false;
    QPoint m_dragStartGlobalPos;
    QPoint m_dragStartWindowPos;
};
