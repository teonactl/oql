#pragma once
#include <QDialog>

class Workspace;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QKeySequenceEdit;
class QSpinBox;
class QFontComboBox;

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
    QSpinBox       *m_rowHeightSpin;
    QFontComboBox  *m_fontFamilyCombo;
    QSpinBox       *m_fontSizeSpin;

    // Progetto
    QLineEdit *m_projectNameEdit;
    QCheckBox *m_showCueNumbersCheck;
    QCheckBox *m_autoFadeOnStopCheck;

    // Scorciatoie
    QKeySequenceEdit *m_keyGoEdit;
    QKeySequenceEdit *m_keyStopAllEdit;
    QKeySequenceEdit *m_keyFirstCueEdit;

    // Remote
    QCheckBox *m_webEnabledCheck;
    QSpinBox  *m_webPortSpin;
};
