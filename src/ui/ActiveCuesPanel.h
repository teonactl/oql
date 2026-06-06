#pragma once
#include <QWidget>
#include <QMap>
#include "engine/Cue.h"

class CueList;
class QVBoxLayout;
class QTimer;
class QProgressBar;
class QLabel;

class ActiveCuesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ActiveCuesPanel(CueList *cueList, QWidget *parent = nullptr);

private slots:
    void onCueStateChanged(int index, Cue::State state);
    void updateProgress();

private:
    struct CardWidgets {
        QLabel       *dotLbl;
        QProgressBar *progressBar;
        QLabel       *elapsedLbl;
        QLabel       *remainingLbl;
    };

    void rebuildCards();

    CueList     *m_cueList;
    QWidget     *m_container;
    QVBoxLayout *m_containerLay;
    QTimer      *m_timer;

    QMap<Cue*, CardWidgets> m_cards;
};
