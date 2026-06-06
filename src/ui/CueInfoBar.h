#pragma once
#include <QFrame>
#include "engine/Cue.h"

class QLabel;
class QTimer;

class CueInfoBar : public QFrame {
    Q_OBJECT
public:
    explicit CueInfoBar(QWidget *parent = nullptr);

    void setCue(Cue *cue);

public slots:
    void refresh();

private:
    void buildUi();
    void updateDisplay();

    Cue    *m_cue = nullptr;
    QTimer *m_timer;

    QLabel *m_dotLbl;
    QLabel *m_numberLbl;
    QLabel *m_typeLbl;
    QLabel *m_nameLbl;
    QLabel *m_fileLbl;
    QLabel *m_timeLbl;
    QLabel *m_volLbl;
    QLabel *m_emptyLbl;
};
