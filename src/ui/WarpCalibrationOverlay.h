#pragma once
#include "engine/ProjectionWarp.h"
#include <QWidget>

class QPushButton;

// Overlay trasparente sovrapposto a una finestra di output (Video/Image) che
// permette di calibrare interattivamente il warp di proiezione: mostra la
// griglia di punti di controllo e lascia l'utente trascinarli con il mouse
// per allineare l'immagine ai bordi reali dello schermo/superficie. Attivo
// solo quando setCalibrating(true) — altrimenti è invisibile e trasparente
// anche ai click, per non interferire con l'uso normale della finestra.
class WarpCalibrationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit WarpCalibrationOverlay(QWidget *parent = nullptr);

    // Il warp non è posseduto da questo overlay: viene editato sul posto
    // (il chiamante decide quando e se persisterlo, es. nel progetto).
    void setWarp(ProjectionWarp *warp);

    bool isCalibrating() const { return m_calibrating; }
    void setCalibrating(bool on);

signals:
    // Emesso durante il trascinamento, per un feedback live sulla finestra
    // sottostante (es. ridisegnare il contenuto warped mentre si trascina).
    void warpEdited();
    // Emesso al rilascio del mouse dopo un trascinamento: è il momento
    // giusto per persistere lo stato (es. su Workspace).
    void warpCommitted();

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mousePressEvent(QMouseEvent *event)  override;
    void mouseMoveEvent(QMouseEvent *event)   override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event)     override;

private:
    void layoutButtons();
    void addColumn();
    void removeColumn();
    void addRow();
    void removeRow();
    void resetWarp();

    ProjectionWarp *m_warp = nullptr;
    bool m_calibrating = false;

    bool m_dragging  = false;
    int  m_dragCol   = -1;
    int  m_dragRow   = -1;
    static constexpr double kHandleRadiusPx = 14.0;

    QPushButton *m_addColBtn;
    QPushButton *m_removeColBtn;
    QPushButton *m_addRowBtn;
    QPushButton *m_removeRowBtn;
    QPushButton *m_resetBtn;
};
