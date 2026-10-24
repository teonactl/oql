#pragma once
#include <QWidget>
#include <QMap>
#include "engine/Cue.h"

class CueList;
class QVBoxLayout;
class QTimer;
class QProgressBar;
class QLabel;
class QFrame;

class ActiveCuesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ActiveCuesPanel(CueList *cueList, QWidget *parent = nullptr);

private slots:
    void onCueStateChanged(int index, Cue::State state);
    void updateProgress();

private:
    struct CardWidgets {
        QFrame       *card;
        QLabel       *dotLbl;
        QProgressBar *progressBar;
        QLabel       *elapsedLbl;
        QLabel       *remainingLbl;
    };

    // E7: prima distruggeva e ricreava TUTTE le card ad ogni singolo
    // cambiamento di stato di QUALSIASI cue (anche una sola che parte/finisce
    // mentre altre 10 sono già in corso) — rebuildCards() ora sincronizza in
    // modo incrementale (aggiunge/rimuove/riposiziona solo quel che serve);
    // buildCard() costruisce la card per UNA cue, usata solo per quelle
    // davvero nuove.
    CardWidgets buildCard(Cue *cue, int indexInList);
    void rebuildCards();

    CueList     *m_cueList;
    QWidget     *m_container;
    QVBoxLayout *m_containerLay;
    QTimer      *m_timer;

    QMap<Cue*, CardWidgets> m_cards;
};
