#pragma once
#include "engine/ProjectionWarp.h"
#include <QWidget>
#include <QImage>

class WarpCalibrationOverlay;
class ProjectionCalibrationToolbar;

// Finestra dedicata alla calibrazione dell'area di proiezione: mostra un
// pattern di riferimento (la griglia incorporata, o un'immagine scelta
// dall'utente) deformato dal warp corrente, con la griglia di punti di
// controllo trascinabile sopra. I controlli (aggiungi/rimuovi riga/colonna,
// reset, scelta immagine) vivono in un pannello flottante separato
// (ProjectionCalibrationToolbar), non qui dentro, per non coprire mai gli
// handle della griglia. Le finestre di Output Video/Immagine restano
// sempre libere da qualsiasi controllo di calibrazione.
class ProjectionCalibrationWindow : public QWidget {
    Q_OBJECT
public:
    explicit ProjectionCalibrationWindow(QWidget *parent = nullptr);

    // Il warp non è posseduto da questa finestra: punta a quello del
    // progetto corrente (Workspace), condiviso con Video/Image Output.
    void setProjectionWarp(ProjectionWarp *warp);

    void toggleFullScreen();

public slots:
    // Mostra/nasconde questa finestra insieme al pannello flottante dei
    // controlli (che altrimenti resterebbe orfano in giro per lo schermo).
    void setCalibrationVisible(bool visible);

signals:
    // Emesso al rilascio di un trascinamento o a un cambio di griglia: il
    // momento giusto per persistere lo stato (es. su Workspace).
    void calibrationCommitted();

protected:
    void paintEvent(QPaintEvent *event)            override;
    void resizeEvent(QResizeEvent *event)          override;
    void moveEvent(QMoveEvent *event)              override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)           override;

private:
    void positionToolbar();
    void chooseCustomImage();
    void useBuiltinPattern();
    QImage currentSourceImage() const;

    ProjectionWarp               *m_warp = nullptr;
    WarpCalibrationOverlay        *m_gridOverlay;
    ProjectionCalibrationToolbar  *m_toolbar;

    // Immagine vuota = usa il pattern a griglia incorporato.
    QImage m_customImage;
};
