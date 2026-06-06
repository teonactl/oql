#pragma once
#include <QWidget>
#include "engine/Cue.h"

class CueList;
class QLabel;
class QLineEdit;
class QTextEdit;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QGroupBox;
class QStackedWidget;
class QComboBox;
class QSpinBox;
class QTimer;
class WaveformView;

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(CueList *cueList, QWidget *parent = nullptr);

    void setCue(Cue *cue);

private slots:
    void onNumberChanged();
    void onNameChanged();
    void onNotesChanged();
    void onPreWaitChanged(double v);
    void onPostWaitChanged(double v);
    void onAutoContinueChanged(bool v);
    void onAutoFollowChanged(bool v);
    void onFilePathChanged();
    void onVolumeChanged(double v);
    void onFadeInChanged(double v);
    void onFadeOutChanged(double v);
    void onFadeInCheckChanged(bool checked);
    void onFadeOutCheckChanged(bool checked);
    void onBrowseFile();
    void onChannelChanged(int idx);
    void onTargetChanged(int idx);
    void onFadeTargetVolChanged(double v);
    void onFadeDurationChanged(double v);
    void onFadeStopAtEndChanged(bool v);
    void onMicDeviceChanged(int idx);
    void onMicVolumeChanged(double v);
    void onSpeedRateChanged(double v);
    void onCuePropertyChanged();
    void onCueStateChanged(Cue::State state);
    void onLoopCountChanged(int v);
    void refreshPlayhead();
    void populateTargetCombo();

private:
    void buildUi();
    void loadFromCue();
    void blockSignals(bool block);
    void updateMediaSection();
    void syncPlayButton(Cue::State state);

    CueList *m_cueList       = nullptr;
    Cue     *m_cue           = nullptr;
    bool     m_loadingFromCue = false;

    // Always visible
    QLabel         *m_typeLabel;
    QPushButton    *m_playBtn  = nullptr;
    QPushButton    *m_stopBtn  = nullptr;
    QLineEdit      *m_numberEdit;
    QLineEdit      *m_nameEdit;
    QTextEdit      *m_notesEdit;
    QDoubleSpinBox *m_preWaitSpin;
    QDoubleSpinBox *m_postWaitSpin;
    QCheckBox      *m_autoContinueCheck;
    QCheckBox      *m_autoFollowCheck;

    // Audio / Video
    QGroupBox      *m_mediaGroup;
    QLineEdit      *m_fileEdit;
    QPushButton    *m_browseBtn;
    QDoubleSpinBox *m_volumeSpin;
    QSpinBox       *m_loopSpin  = nullptr;

    // Audio-only fade
    QGroupBox      *m_fadeGroup;
    QCheckBox      *m_fadeInCheck;
    QDoubleSpinBox *m_fadeInSpin;
    QCheckBox      *m_fadeOutCheck;
    QDoubleSpinBox *m_fadeOutSpin;
    QComboBox      *m_channelCombo;
    WaveformView   *m_waveformView;
    QTimer         *m_playTimer;
    QWidget        *m_audioSection;

    // Control cues (Stop / Fade / Pause / Speed)
    QWidget        *m_controlSection;
    QComboBox      *m_targetCombo;
    QGroupBox      *m_fadeParamsGroup;
    QDoubleSpinBox *m_fadeTargetVolSpin;
    QDoubleSpinBox *m_fadeDurationSpin;
    QCheckBox      *m_fadeStopAtEndCheck = nullptr;
    QGroupBox      *m_speedGroup     = nullptr;
    QDoubleSpinBox *m_speedRateSpin  = nullptr;

    // Mic cue
    QWidget        *m_micSection     = nullptr;
    QComboBox      *m_micDeviceCombo = nullptr;
    QDoubleSpinBox *m_micVolumeSpin  = nullptr;

    QWidget        *m_emptyWidget;
    QStackedWidget *m_stack;
};
