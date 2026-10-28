#pragma once
#include <QWidget>

class QPushButton;

// Pannello di controlli per la calibrazione, in una finestra flottante
// separata (Qt::Tool) — non sovrapposta al contenuto della finestra di
// calibrazione, così non copre mai gli handle della griglia vicino ai
// bordi/angoli. Segue a vista ProjectionCalibrationWindow (mostrata/
// nascosta insieme a essa) ma è una finestra indipendente, spostabile.
class ProjectionCalibrationToolbar : public QWidget {
    Q_OBJECT
public:
    explicit ProjectionCalibrationToolbar(QWidget *parent = nullptr);

    QPushButton *addColumnButton()    { return m_addColBtn; }
    QPushButton *removeColumnButton() { return m_removeColBtn; }
    QPushButton *addRowButton()       { return m_addRowBtn; }
    QPushButton *removeRowButton()    { return m_removeRowBtn; }
    QPushButton *resetButton()        { return m_resetBtn; }
    QPushButton *loadImageButton()    { return m_loadImageBtn; }
    QPushButton *useBuiltinButton()   { return m_useBuiltinBtn; }
    QPushButton *closeButton()        { return m_closeBtn; }

private:
    QPushButton *m_addColBtn;
    QPushButton *m_removeColBtn;
    QPushButton *m_addRowBtn;
    QPushButton *m_removeRowBtn;
    QPushButton *m_resetBtn;
    QPushButton *m_loadImageBtn;
    QPushButton *m_useBuiltinBtn;
    // Chiusura esplicita e visibile: a schermo intero (come si apre di
    // default, vedi MainWindow::rebuildCalibrationMenu) non c'è nessuna
    // barra del titolo da cui chiudere altrimenti.
    QPushButton *m_closeBtn;
};
