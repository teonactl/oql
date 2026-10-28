#pragma once
#include <QDialog>
#include <QStringList>

class Workspace;
class Cue;

// Scorciatoia dall'Inspector di una cue video/immagine: invece di aprire
// "Gestione schermi…" e navigare fino allo schermo giusto, mostra qui
// direttamente adattamento (fitMode) e posizione (contentAnchor) di ogni
// VirtualScreen a cui la cue è assegnata — scrive sugli stessi dati di
// VirtualScreen (vedi engine/VirtualScreen.h), non introduce uno stato
// separato per-cue: se più cue condividono uno schermo, cambiare
// l'adattamento da qui lo cambia per tutte (coerente con ScreensManagerDialog).
// Mostra anche un'anteprima trascinabile (ScreenFitPreview) del contenuto
// DI QUESTA CUE per ciascuno schermo, usando la stessa resa della finestra
// di output reale.
class ScreenFitDialog : public QDialog {
    Q_OBJECT
public:
    // screenIds vuoto = la cue non è assegnata a schermi specifici (si
    // mostra ovunque): in quel caso mostra tutti gli schermi del progetto,
    // con una nota esplicativa. `cue` è la VideoCue/ImageCue per cui si sta
    // regolando l'adattamento — usata solo per l'anteprima (può essere
    // nullptr, l'anteprima mostra allora un segnaposto).
    explicit ScreenFitDialog(Workspace *workspace, Cue *cue, const QStringList &screenIds, QWidget *parent = nullptr);
};
