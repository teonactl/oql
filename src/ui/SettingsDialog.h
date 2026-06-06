#pragma once
#include <QDialog>

class Workspace;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Workspace *workspace, QWidget *parent = nullptr);

private slots:
    void apply();

private:
    Workspace *m_workspace;

    // Generale (app-level)
    QDoubleSpinBox *m_fadeDurationSpin;
    QDoubleSpinBox *m_fadeInSpin;
    QDoubleSpinBox *m_fadeOutSpin;
    QCheckBox      *m_autoNumberCheck;

    // Progetto
    QLineEdit *m_projectNameEdit;
    QCheckBox *m_showCueNumbersCheck;
    QCheckBox *m_autoFadeOnStopCheck;
};
