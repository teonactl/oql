#pragma once
#include <QWidget>
#include <QDialog>
#include "engine/Cue.h"

class CueList;
class AudioCue;
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
class QFontComboBox;
class PluginChainWidget;
class VuMeter;
class QPlainTextEdit;
class QTableWidget;

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
    void onEffectDurationChanged(double v);
    void onCuePropertyChanged();
    void onCueStateChanged(Cue::State state);
    void onLoopCountChanged(int v);
    void refreshPlayhead();
    void populateTargetCombo();
    void onTextContentChanged();
    void onFontFamilyChanged(const QFont &f);
    void onFontSizeChanged(int v);
    void onBoldChanged(bool v);
    void onItalicChanged(bool v);
    void onTextColorClicked();
    void onBgColorClicked();
    void onTextAlignChanged(int idx);

private:
    void buildUi();
    void loadFromCue();
    void blockSignals(bool block);
    void updateMediaSection();
    void syncPlayButton(Cue::State state);
    QVector<double>  buildSliceMarkers(AudioCue *a) const;
    void             rebuildSliceTable(AudioCue *a);

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
    VuMeter        *m_vuL          = nullptr;
    VuMeter        *m_vuR          = nullptr;
    QTimer         *m_playTimer;
    QTimer         *m_vuTimer      = nullptr;
    QWidget        *m_audioSection;
    QPushButton    *m_fxBtn        = nullptr;
    QDialog        *m_fxDialog     = nullptr;

    // Control cues (Stop / Fade / Pause / Speed / Effect / ResetEffect)
    QWidget        *m_controlSection;
    QComboBox      *m_targetCombo;
    QGroupBox      *m_fadeParamsGroup;
    QDoubleSpinBox *m_fadeTargetVolSpin;
    QDoubleSpinBox *m_fadeDurationSpin;
    QCheckBox      *m_fadeStopAtEndCheck = nullptr;
    QGroupBox      *m_speedGroup     = nullptr;
    QDoubleSpinBox *m_speedRateSpin  = nullptr;
    QPushButton    *m_effectFxBtn      = nullptr;
    QDialog        *m_effectFxDialog   = nullptr;
    QGroupBox      *m_effectGroup      = nullptr;
    QDoubleSpinBox *m_effectDurSpin    = nullptr;

    // Mic cue
    QWidget        *m_micSection     = nullptr;
    QComboBox      *m_micDeviceCombo = nullptr;
    QDoubleSpinBox *m_micVolumeSpin  = nullptr;

    // Text cue
    QWidget        *m_textSection    = nullptr;
    QTextEdit      *m_textContent    = nullptr;
    QFontComboBox  *m_fontFamilyCombo = nullptr;
    QSpinBox       *m_fontSizeSpin   = nullptr;
    QCheckBox      *m_textBoldCheck  = nullptr;
    QCheckBox      *m_textItalicCheck = nullptr;
    QPushButton    *m_textColorBtn   = nullptr;
    QPushButton    *m_textBgColorBtn = nullptr;
    QComboBox      *m_textAlignCombo = nullptr;

    // Slice / rate controls (audio cue only)
    QWidget        *m_sliceSection   = nullptr;
    QTableWidget   *m_sliceTable     = nullptr;
    QPushButton    *m_addSliceBtn    = nullptr;
    QPushButton    *m_clearSlicesBtn = nullptr;
    QDoubleSpinBox *m_rateSpin       = nullptr;

    PluginChainWidget *m_pluginChainWidget       = nullptr;  // AudioCue chain editor
    PluginChainWidget *m_effectPluginChainWidget = nullptr;  // EffectCue chain editor

    // Script cue
    QWidget     *m_scriptSection  = nullptr;
    QPushButton *m_scriptRunBtn   = nullptr;

    QWidget        *m_emptyWidget;
    QStackedWidget *m_stack;
};
