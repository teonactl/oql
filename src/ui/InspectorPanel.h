#pragma once
#include <QWidget>
#include <QDialog>
#include <QVector>
#include <QPointer>
#include "engine/Cue.h"

class CueList;
class AudioCue;
class QScrollArea;
class QVBoxLayout;
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
class QSlider;
class QTimer;
class WaveformView;
class QFontComboBox;
class QFormLayout;
class PluginChainWidget;
class VuMeter;
class QPlainTextEdit;
class QTableWidget;
class QListWidget;
class ImageCue;
class Workspace;

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(CueList *cueList, QWidget *parent = nullptr);

    // Necessario solo per popolare le liste "Schermi" delle cue video/
    // immagine (engine/VirtualScreen.h) — non posseduto.
    void setWorkspace(Workspace *ws);

    void setCue(Cue *cue);
    void setShowMode(bool showMode);
    void addShowAudioCue(AudioCue *cue);
    void removeShowAudioCue(AudioCue *cue);
    QSize sizeHint() const override;

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
    void onOutputDeviceChanged(int index);
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
    void onFadeChannelChanged(int idx);
    void onFadeTargetVisualChanged(double v);
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
    void onSlideDurationChanged(double v);
    void onTransitionDurationChanged(double v);
    void onTransitionTypeChanged(int idx);
    void onLoopChanged(bool v);
    void onAddImages();
    void onRemoveSelectedImages();
    void onImageListReordered();
    void onBackgroundChanged(int idx);
    void onChooseBackgroundImage();
    void onImageContentModeChanged(int idx);
    void onImageSolidColorClicked();
    void onImageGradientColor1Clicked();
    void onImageGradientColor2Clicked();
    void onImageGradientAngleChanged(double v);
    void onVideoScreensChanged();
    void onImageScreensChanged();
    void onVideoFadeInChanged(double v);
    void onVideoFadeOutChanged(double v);
    void onVideoFitButtonClicked();
    void onImageFitButtonClicked();
    void onVideoMuteToggled(bool checked);
    void onVideoVolumeSliderChanged(int value);
    void onVideoBackgroundChanged(int idx);
    void onChooseVideoBackgroundImage();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslateUi();
    void loadFromCue();
    // R10 (nota a parte): nascondeva QObject::blockSignals(bool) con
    // semantica diversa (blocca i segnali DEI CAMPI dell'inspector, non i
    // propri) — rinominata per evitare l'ambiguità.
    void blockFieldSignals(bool block);
    void updateMediaSection();
    // Mostra/nasconde canale, volume audio (dB) e livello video (%) della
    // sezione Fade in base al tipo del target corrente e al canale scelto
    // — richiamata sia quando si seleziona una cue Fade sia quando si
    // cambia target o canale sul posto, così i due spinbox mai pertinenti
    // insieme (dB per audio-only, % per video-only) non restano visibili a
    // vuoto.
    void updateFadeTargetFieldsVisibility();
    void refreshImageList();
    void syncPlayButton(Cue::State state);
    QVector<double>  buildSliceMarkers(AudioCue *a) const;
    void             rebuildSliceTable(AudioCue *a);

    CueList   *m_cueList     = nullptr;
    Workspace *m_workspace   = nullptr;
    // QPointer, non Cue*: si azzera da solo se la cue puntata viene distrutta
    // altrove (undo/redo, eliminazione) senza passare da setCue(nullptr).
    QPointer<Cue> m_cue;
    bool     m_loadingFromCue = false;
    bool     m_showMode       = false;

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
    QComboBox      *m_outputDeviceCombo = nullptr;  // uscita fisica (solo AudioCue)
    QSpinBox       *m_loopSpin  = nullptr;
    QComboBox      *m_videoBgCombo    = nullptr;
    QPushButton    *m_videoBgImageBtn = nullptr;

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
    QLabel         *m_vuLDbLabel   = nullptr;
    QLabel         *m_vuRDbLabel   = nullptr;
    QLabel         *m_recSourceLabel = nullptr;
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
    QComboBox      *m_fadeChannelCombo   = nullptr;
    QDoubleSpinBox *m_fadeTargetVisualSpin = nullptr;
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

    // Image cue (slideshow)
    QWidget        *m_imageSection           = nullptr;
    QGroupBox      *m_imageGroup             = nullptr;
    QFormLayout    *m_imageForm              = nullptr;
    QPushButton    *m_imagesBtn              = nullptr;
    QDoubleSpinBox *m_slideDurationSpin      = nullptr;
    QDoubleSpinBox *m_transitionDurationSpin = nullptr;
    QComboBox      *m_transitionCombo        = nullptr;
    QCheckBox      *m_loopCheck              = nullptr;
    QComboBox      *m_backgroundCombo        = nullptr;
    QPushButton    *m_backgroundImageBtn     = nullptr;

    // Image cue — contenuto (file / colore pieno / gradiente)
    QGroupBox      *m_imageContentGroup      = nullptr;
    QFormLayout    *m_imageContentForm       = nullptr;
    QComboBox      *m_imageContentModeCombo  = nullptr;
    QPushButton    *m_imageSolidColorBtn     = nullptr;
    QPushButton    *m_imageGradientColor1Btn = nullptr;
    QPushButton    *m_imageGradientColor2Btn = nullptr;
    QDoubleSpinBox *m_imageGradientAngleSpin = nullptr;
    QListWidget    *m_imageScreensList       = nullptr;
    QListWidget    *m_videoScreensList       = nullptr;
    QDoubleSpinBox *m_videoFadeInSpin        = nullptr;
    QDoubleSpinBox *m_videoFadeOutSpin       = nullptr;
    QPushButton    *m_videoFitBtn            = nullptr;
    QPushButton    *m_imageFitBtn            = nullptr;
    QPushButton    *m_videoMuteBtn           = nullptr;
    QSlider        *m_videoVolumeSlider      = nullptr;
    QDialog        *m_imageListDialog        = nullptr;   // non-modal: lista immagini riordinabile
    QListWidget    *m_imageList              = nullptr;
    QPushButton    *m_addImagesBtn           = nullptr;
    QPushButton    *m_removeImagesBtn        = nullptr;
    QLabel         *m_imageCountLbl          = nullptr;

    // Slice dialog (audio cue only)
    QDialog        *m_sliceDialog    = nullptr;   // non-modal dialog with slice table
    QTableWidget   *m_sliceTable     = nullptr;
    QPushButton    *m_addSliceBtn    = nullptr;
    QPushButton    *m_clearSlicesBtn = nullptr;
    QPushButton    *m_slicesBtn      = nullptr;   // "Segmenti (N)…" in inspector
    QLabel         *m_sliceCountLbl  = nullptr;   // count label inside dialog
    QDoubleSpinBox *m_rateSpin       = nullptr;

    PluginChainWidget *m_pluginChainWidget       = nullptr;  // AudioCue chain editor
    PluginChainWidget *m_effectPluginChainWidget = nullptr;  // EffectCue chain editor

    // Script cue
    QWidget     *m_scriptSection  = nullptr;
    QPushButton *m_scriptRunBtn   = nullptr;

    // Record cue
    QWidget   *m_recordSection    = nullptr;
    QComboBox *m_recDeviceCombo   = nullptr;
    QComboBox *m_recTargetCombo   = nullptr;
    QLabel    *m_recPathLabel     = nullptr;
    VuMeter   *m_recVuMono        = nullptr;
    QLabel    *m_recVuDbLabel     = nullptr;
    QTimer    *m_recVuTimer       = nullptr;

    QLabel         *m_emptyLabel        = nullptr;
    QGroupBox      *m_genGroup          = nullptr;
    QFormLayout    *m_genForm           = nullptr;
    QGroupBox      *m_timeGroup         = nullptr;
    QFormLayout    *m_timeForm          = nullptr;
    QFormLayout    *m_mediaForm         = nullptr;
    QFormLayout    *m_fadeForm          = nullptr;
    QGroupBox      *m_targetGroup       = nullptr;
    QFormLayout    *m_targetForm        = nullptr;
    QFormLayout    *m_fadeParamsForm    = nullptr;
    QFormLayout    *m_speedForm         = nullptr;
    QFormLayout    *m_effectForm        = nullptr;
    QGroupBox      *m_micGroup          = nullptr;
    QFormLayout    *m_micForm           = nullptr;
    QGroupBox      *m_textContentGroup  = nullptr;
    QGroupBox      *m_textFmtGroup      = nullptr;
    QFormLayout    *m_textFmtForm       = nullptr;
    QGroupBox      *m_recGroup          = nullptr;
    QFormLayout    *m_recForm           = nullptr;
    QLabel         *m_uscitaLabel       = nullptr;
    QLabel         *m_rateLabel         = nullptr;
    QLabel         *m_slicesLabel       = nullptr;  // "Slices:" label in extras panel
    QLabel         *m_recLevelLabel     = nullptr;
    QCheckBox      *m_pitchCheck        = nullptr;

    QWidget        *m_emptyWidget;
    QStackedWidget *m_stack;
    QWidget        *m_audioExtrasPanel  = nullptr;

    // Show-mode visibility references (normal-mode widgets hidden in show mode)
    QWidget        *m_groupsRow   = nullptr;
    QWidget        *m_chanRow     = nullptr;
    QWidget        *m_fxRow       = nullptr;
    QWidget        *m_headerRow   = nullptr;

    // Show-mode stacked waveforms
    struct ShowRow { AudioCue *cue; QWidget *widget; WaveformView *wv; };
    QVector<ShowRow>  m_showRows;
    QScrollArea      *m_showModeArea       = nullptr;
    QVBoxLayout      *m_showModeContentLay = nullptr;
    QTimer           *m_showPlayTimer      = nullptr;
};
