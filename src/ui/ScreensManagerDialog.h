#pragma once
#include <QDialog>
#include <QString>

class Workspace;
class QListWidget;
class QListWidgetItem;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class PhysicalOutputZonesCanvas;

// Gestione (creazione/rimozione/assegnazione) degli output fisici e degli
// schermi virtuali del progetto corrente — l'interfaccia che rende
// utilizzabile il modello PhysicalOutput/VirtualScreen (vedi
// engine/VirtualScreen.h): senza questa finestra i dati esistevano già ma
// non c'era modo per l'utente di crearne più di uno o di dargli una forma.
//
// Finestra non modale: le modifiche (aggiungi/rimuovi/rinomina output o
// schermo, risoluzione, canvasRect, poligono) si applicano subito al
// Workspace — non c'è un "Annulla" complessivo, coerente con come funziona
// già la calibrazione di proiezione (ProjectionCalibrationWindow).
class ScreensManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreensManagerDialog(Workspace *workspace, QWidget *parent = nullptr);

    // Da richiamare quando il progetto cambia (nuovo/caricato) o quando gli
    // output/schermi cambiano da altrove (es. migrazione al caricamento).
    void refreshAll();

private slots:
    void onOutputSelectionChanged();
    void onScreenSelectionChanged();
    void onAddOutput();
    void onRemoveOutput();
    void onRenameOutput();
    void onOutputScreenNameChanged(int idx);
    void onAutoResolutionToggled(bool on);
    void onResolutionSpinChanged();
    void onDetectResolution();
    void onAddScreen();
    void onRemoveScreen();
    void onRenameScreen();
    void onCanvasRectChanged();
    void onFitModeChanged(int idx);
    void onAnchorChanged();
    void onMaskEdited();
    void onCanvasSelectionChanged(const QString &screenId);
    void onApplyPreset();

private:
    void refreshOutputList(const QString &selectId = {});
    void refreshScreenList(const QString &selectId = {});
    void refreshScreenFields();
    void refreshRealScreenInfo();
    QString currentOutputId() const;
    QString currentScreenId() const;

    Workspace *m_workspace;

    QListWidget *m_outputList;
    QPushButton *m_addOutputBtn;
    QPushButton *m_removeOutputBtn;
    QPushButton *m_renameOutputBtn;
    QComboBox   *m_outputScreenCombo;
    QLabel      *m_realScreenInfoLabel;

    QCheckBox      *m_autoResolutionCheck;
    QSpinBox       *m_resWSpin;
    QSpinBox       *m_resHSpin;
    QPushButton    *m_detectResolutionBtn;

    QComboBox   *m_presetCombo;
    QPushButton *m_applyPresetBtn;

    QListWidget *m_screenList;
    QPushButton *m_addScreenBtn;
    QPushButton *m_removeScreenBtn;
    QPushButton *m_renameScreenBtn;

    QDoubleSpinBox *m_canvasXSpin;
    QDoubleSpinBox *m_canvasYSpin;
    QDoubleSpinBox *m_canvasWSpin;
    QDoubleSpinBox *m_canvasHSpin;

    QComboBox      *m_fitModeCombo;
    QDoubleSpinBox *m_anchorXSpin;
    QDoubleSpinBox *m_anchorYSpin;

    PhysicalOutputZonesCanvas *m_zonesCanvas;
    QPushButton *m_addVertexBtn;
    QPushButton *m_removeVertexBtn;
    QPushButton *m_resetMaskBtn;

    bool m_loading = false; // sopprime le callback durante il refresh dei campi
};
