#pragma once
#include "engine/ProjectionWarp.h"
#include <QWidget>

// Overlay trasparente sovrapposto alla finestra di calibrazione che mostra
// la griglia di punti di controllo e lascia l'utente trascinarli con il
// mouse per allineare l'immagine ai bordi reali dello schermo/superficie.
// Nessun controllo (pulsanti ecc.) qui dentro: vivono in
// ProjectionCalibrationToolbar, una finestra flottante separata, così non
// coprono mai gli handle vicino ai bordi — vedi quel file.
class WarpCalibrationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit WarpCalibrationOverlay(QWidget *parent = nullptr);

    // Il warp non è posseduto da questo overlay: viene editato sul posto
    // (il chiamante decide quando e se persisterlo, es. nel progetto).
    void setWarp(ProjectionWarp *warp);

signals:
    // Emesso durante il trascinamento, per un feedback live sulla finestra
    // sottostante (es. ridisegnare il contenuto warped mentre si trascina).
    void warpEdited();
    // Emesso al rilascio del mouse dopo un trascinamento, o dopo un cambio
    // di dimensione/reset della griglia: il momento giusto per persistere
    // lo stato (es. su Workspace).
    void warpCommitted();

public slots:
    void addColumn();
    void removeColumn();
    void addRow();
    void removeRow();
    void resetWarp();

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mousePressEvent(QMouseEvent *event)  override;
    void mouseMoveEvent(QMouseEvent *event)   override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Posizione in pixel a cui disegnare/intercettare l'handle del punto
    // (col,row): per i punti sul bordo della griglia è spostata verso
    // l'interno di kEdgeInsetPx rispetto alla posizione vera (che resta
    // invece quella usata per le linee della mesh) — altrimenti un punto
    // esattamente a x=0 o y=altezza finisce a metà tagliato dal bordo della
    // finestra ed è scomodo da afferrare col mouse.
    QPointF handlePixelPos(int col, int row) const;

    ProjectionWarp *m_warp = nullptr;

    bool m_dragging  = false;
    int  m_dragCol   = -1;
    int  m_dragRow   = -1;
    static constexpr double kHandleRadiusPx = 14.0;
    static constexpr double kEdgeInsetPx    = 18.0;
};
