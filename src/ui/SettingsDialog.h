#pragma once
#include <QDialog>
#include <QMap>
#include <QString>

class Workspace;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QKeySequenceEdit;
class QSpinBox;
class QFontComboBox;
class QComboBox;
class QListWidget;

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
    QComboBox      *m_activeCueSideCombo;
    QComboBox      *m_languageCombo;
    QComboBox      *m_waveformBucketsCombo;

    // Progetto
    QLineEdit *m_projectNameEdit;
    QCheckBox *m_showCueNumbersCheck;
    QCheckBox *m_autoFadeOnStopCheck;

    // Scorciatoie trasporto
    QKeySequenceEdit *m_keyGoEdit;
    QKeySequenceEdit *m_keyStopAllEdit;
    QKeySequenceEdit *m_keyFirstCueEdit;
    QKeySequenceEdit *m_keyShowModeEdit;

    // Scorciatoie aggiungi cue (key = typeKey string)
    QMap<QString, QKeySequenceEdit*> m_keyAddCueEdits;

    // Remote
    QCheckBox *m_webEnabledCheck;
    QSpinBox  *m_webPortSpin;

    // Plugin
    QListWidget *m_lv2PathsList;
    QListWidget *m_vstPathsList;

    // Hardware
    QComboBox *m_audioDeviceCombo;
    QComboBox *m_outputScreenCombo;
};
