#include "InspectorPanel.h"
#include "WaveformView.h"
#include "engine/ScriptCue.h"
#include "engine/ScriptEngine.h"
#include "ScriptEditorDialog.h"
#include "PluginChainWidget.h"
#include "engine/AudioCue.h"
#include "engine/AudioEngine.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"
#include "engine/MicCue.h"
#include "engine/TextCue.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include "engine/CueList.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QTimer>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QPainter>
#include <cmath>
#include <algorithm>
#include <QFontComboBox>
#include <QColorDialog>
#include <QTableWidget>
#include <QHeaderView>

// ── VU meter widget ───────────────────────────────────────────────────────────

class VuMeter : public QWidget {
public:
    explicit VuMeter(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(14);
        setMinimumHeight(80);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    void setLevel(float linear) {
        const float db = (linear <= 1e-7f) ? -60.0f
                       : std::max(-60.0f, 20.0f * std::log10f(linear));
        m_env = (db > m_env) ? db : m_env + (db - m_env) * 0.25f;
        if (db >= m_peak) { m_peak = db; m_holdTicks = 50; }
        else if (m_holdTicks > 0) --m_holdTicks;
        else if (m_peak > -60.0f) m_peak -= 0.8f;
        update();
    }
private:
    float m_env   = -60.0f;
    float m_peak  = -60.0f;
    int   m_holdTicks = 0;

    static float frac(float db) { return (db + 60.0f) / 66.0f; }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const int W = width(), H = height();
        p.fillRect(0, 0, W, H, QColor(18, 18, 18));

        const float f = std::clamp(frac(m_env), 0.0f, 1.0f);
        if (f < 0.001f) return;
        const int bY = H - int(f * H);

        const int yRed    = H - int(frac(-6.0f)  * H);
        const int yYellow = H - int(frac(-18.0f) * H);

        if (bY < yRed)
            p.fillRect(0, bY, W, yRed - bY, QColor(220, 40, 40));
        const int yT = std::max(bY, yRed);
        if (yT < yYellow)
            p.fillRect(0, yT, W, yYellow - yT, QColor(200, 170, 20));
        const int gT = std::max(bY, yYellow);
        if (gT < H)
            p.fillRect(0, gT, W, H - gT, QColor(40, 180, 40));

        const float fp = std::clamp(frac(m_peak), 0.0f, 1.0f);
        if (fp > 0.01f) {
            const int pY = std::clamp(H - int(fp * H) - 1, 0, H - 2);
            p.fillRect(0, pY, W, 2,
                       fp > frac(-6.0f) ? QColor(255, 100, 100) : QColor(220, 220, 60));
        }
    }
};

// ── dB helpers ────────────────────────────────────────────────────────────────

static double linearToDb(double v) {
    return v <= 0.0 ? -60.0 : std::max(-60.0, 20.0 * std::log10(v));
}
static double dbToLinear(double db) {
    return db <= -60.0 ? 0.0 : std::pow(10.0, db / 20.0);
}

// ─────────────────────────────────────────────────────────────────────────────

InspectorPanel::InspectorPanel(CueList *cueList, QWidget *parent)
    : QWidget(parent), m_cueList(cueList)
{
    buildUi();
    setCue(nullptr);

    if (m_cueList) {
        connect(m_cueList, &CueList::cueAdded,           this, &InspectorPanel::populateTargetCombo);
        connect(m_cueList, &CueList::cueRemoved,         this, &InspectorPanel::populateTargetCombo);
        connect(m_cueList, &CueList::cuePropertyChanged, this, &InspectorPanel::populateTargetCombo);
    }
}

void InspectorPanel::buildUi() {
    setMinimumHeight(150);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(4);

    // ── Empty state ──────────────────────────────────────────
    m_emptyWidget = new QWidget;
    auto *emptyLay = new QHBoxLayout(m_emptyWidget);
    auto *emptyLbl = new QLabel("Seleziona una cue per vedere le proprietà");
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLbl->setStyleSheet("color: #888; font-style: italic;");
    emptyLay->addWidget(emptyLbl);

    // ── Properties widget ────────────────────────────────────
    auto *propsWidget = new QWidget;
    auto *propsLay    = new QVBoxLayout(propsWidget);
    propsLay->setContentsMargins(0, 0, 0, 0);
    propsLay->setSpacing(4);

    // Header row: type label + stop + play/pause buttons
    auto *headerRow = new QWidget;
    auto *headerLay = new QHBoxLayout(headerRow);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(4);
    m_typeLabel = new QLabel;
    m_typeLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #555;");
    m_stopBtn = new QPushButton("■");
    m_stopBtn->setFixedWidth(32);
    m_stopBtn->setToolTip("Stop");
    m_playBtn = new QPushButton("▶  Play");
    m_playBtn->setFixedWidth(100);
    m_playBtn->setToolTip("Play / Pausa / Riprendi");
    headerLay->addWidget(m_typeLabel);
    headerLay->addStretch();
    headerLay->addWidget(m_stopBtn);
    headerLay->addWidget(m_playBtn);
    propsLay->addWidget(headerRow);

    // ── Groups row (horizontal) ──────────────────────────────
    auto *groupsRow = new QWidget;
    auto *groupsLay = new QHBoxLayout(groupsRow);
    groupsLay->setContentsMargins(0, 0, 0, 0);
    groupsLay->setSpacing(6);

    auto makeSpin = [](double max = 999.0) {
        auto *s = new QDoubleSpinBox;
        s->setRange(0, max);
        s->setSingleStep(0.1);
        s->setDecimals(2);
        s->setSuffix(" s");
        return s;
    };

    // General group
    auto *genGroup = new QGroupBox("Generale");
    genGroup->setMinimumWidth(185);
    auto *genForm  = new QFormLayout(genGroup);
    genForm->setSpacing(3);

    m_numberEdit = new QLineEdit;
    m_numberEdit->setMaximumWidth(80);
    m_nameEdit   = new QLineEdit;
    m_notesEdit  = new QTextEdit;
    m_notesEdit->setMaximumHeight(38);
    m_notesEdit->setPlaceholderText("Note...");

    genForm->addRow("Numero:", m_numberEdit);
    genForm->addRow("Nome:",   m_nameEdit);
    genForm->addRow("Note:",   m_notesEdit);
    groupsLay->addWidget(genGroup);

    // Timing group
    auto *timeGroup = new QGroupBox("Timing");
    timeGroup->setMinimumWidth(195);
    auto *timeForm  = new QFormLayout(timeGroup);
    timeForm->setSpacing(3);

    m_preWaitSpin  = makeSpin();
    m_postWaitSpin = makeSpin();
    m_autoContinueCheck = new QCheckBox("Auto-continue");
    m_autoFollowCheck   = new QCheckBox("Auto-follow");

    timeForm->addRow("Pre-wait:",  m_preWaitSpin);
    timeForm->addRow("Post-wait:", m_postWaitSpin);
    timeForm->addRow(m_autoContinueCheck);
    timeForm->addRow(m_autoFollowCheck);
    groupsLay->addWidget(timeGroup);

    // Media group (audio + video)
    m_mediaGroup = new QGroupBox("File sorgente");
    m_mediaGroup->setMinimumWidth(220);
    auto *mediaForm = new QFormLayout(m_mediaGroup);
    mediaForm->setSpacing(3);

    auto *fileRow = new QWidget;
    auto *fileRLay = new QHBoxLayout(fileRow);
    fileRLay->setContentsMargins(0, 0, 0, 0);
    m_fileEdit  = new QLineEdit;
    m_fileEdit->setReadOnly(true);
    m_fileEdit->setPlaceholderText("Nessun file...");
    m_browseBtn = new QPushButton("...");
    m_browseBtn->setFixedWidth(30);
    fileRLay->addWidget(m_fileEdit);
    fileRLay->addWidget(m_browseBtn);

    m_volumeSpin = new QDoubleSpinBox;
    m_volumeSpin->setRange(-60.0, 0.0);
    m_volumeSpin->setSingleStep(0.5);
    m_volumeSpin->setDecimals(1);
    m_volumeSpin->setSuffix(" dB");

    m_loopSpin = new QSpinBox;
    m_loopSpin->setRange(0, 99);
    m_loopSpin->setSpecialValueText("∞");
    m_loopSpin->setToolTip("Ripetizioni: 0 = loop infinito, 1 = una volta");
    m_loopSpin->setFixedWidth(60);

    mediaForm->addRow("File:", fileRow);
    mediaForm->addRow("Volume:", m_volumeSpin);
    mediaForm->addRow("Loop:", m_loopSpin);
    groupsLay->addWidget(m_mediaGroup);

    // Fade group (audio only) — with enable/disable checkboxes
    m_fadeGroup = new QGroupBox("Fade");
    m_fadeGroup->setMinimumWidth(175);
    auto *fadeForm = new QFormLayout(m_fadeGroup);
    fadeForm->setSpacing(3);

    auto makeFadeRow = [&](QCheckBox *&chk, QDoubleSpinBox *&spin, const QString &label) {
        auto *row    = new QWidget;
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(4);
        chk  = new QCheckBox;
        chk->setChecked(true);
        spin = makeSpin();
        rowLay->addWidget(chk);
        rowLay->addWidget(spin, 1);
        fadeForm->addRow(label, row);
    };

    makeFadeRow(m_fadeInCheck,  m_fadeInSpin,  "In:");
    makeFadeRow(m_fadeOutCheck, m_fadeOutSpin, "Out:");
    groupsLay->addWidget(m_fadeGroup);

    groupsLay->addStretch();
    propsLay->addWidget(groupsRow);

    // ── Control cue section (Stop / Fade / Pause) ────────────
    m_controlSection = new QWidget;
    auto *ctrlLay = new QVBoxLayout(m_controlSection);
    ctrlLay->setContentsMargins(0, 2, 0, 0);
    ctrlLay->setSpacing(4);

    auto *ctrlRow = new QWidget;
    auto *ctrlRowLay = new QHBoxLayout(ctrlRow);
    ctrlRowLay->setContentsMargins(0, 0, 0, 0);
    ctrlRowLay->setSpacing(6);

    auto *targetGroup = new QGroupBox("Cue Target");
    auto *targetForm  = new QFormLayout(targetGroup);
    targetForm->setSpacing(3);
    m_targetCombo = new QComboBox;
    m_targetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    targetForm->addRow("Target:", m_targetCombo);
    ctrlRowLay->addWidget(targetGroup);

    m_fadeParamsGroup = new QGroupBox("Parametri Fade");
    auto *fadeParamsForm = new QFormLayout(m_fadeParamsGroup);
    fadeParamsForm->setSpacing(3);
    m_fadeTargetVolSpin = new QDoubleSpinBox;
    m_fadeTargetVolSpin->setRange(-60.0, 0.0);
    m_fadeTargetVolSpin->setSingleStep(0.5);
    m_fadeTargetVolSpin->setDecimals(1);
    m_fadeTargetVolSpin->setSuffix(" dB");
    m_fadeDurationSpin = new QDoubleSpinBox;
    m_fadeDurationSpin->setRange(0.01, 999.0);
    m_fadeDurationSpin->setSingleStep(0.1);
    m_fadeDurationSpin->setDecimals(2);
    m_fadeDurationSpin->setSuffix(" s");
    m_fadeStopAtEndCheck = new QCheckBox("Stop al termine del fade");
    fadeParamsForm->addRow("Volume target:", m_fadeTargetVolSpin);
    fadeParamsForm->addRow("Durata:",        m_fadeDurationSpin);
    fadeParamsForm->addRow(m_fadeStopAtEndCheck);
    ctrlRowLay->addWidget(m_fadeParamsGroup);

    m_speedGroup = new QGroupBox("Velocità");
    auto *speedForm = new QFormLayout(m_speedGroup);
    speedForm->setSpacing(3);
    m_speedRateSpin = new QDoubleSpinBox;
    m_speedRateSpin->setRange(0.1, 10.0);
    m_speedRateSpin->setSingleStep(0.1);
    m_speedRateSpin->setDecimals(2);
    m_speedRateSpin->setSuffix(" ×");
    m_speedRateSpin->setToolTip("< 1 = rallenta, > 1 = velocizza");
    speedForm->addRow("Fattore:", m_speedRateSpin);
    ctrlRowLay->addWidget(m_speedGroup);
    ctrlRowLay->addStretch();

    ctrlLay->addWidget(ctrlRow);

    // Effect cue: duration + button to open the plugin chain editor
    m_effectGroup = new QGroupBox("Parametri Effetto");
    auto *effectForm = new QFormLayout(m_effectGroup);
    effectForm->setSpacing(3);
    m_effectDurSpin = new QDoubleSpinBox;
    m_effectDurSpin->setRange(0.0, 9999.0);
    m_effectDurSpin->setSingleStep(0.1);
    m_effectDurSpin->setDecimals(2);
    m_effectDurSpin->setSuffix(" s");
    m_effectDurSpin->setSpecialValueText("∞ (nessun reset auto)");
    m_effectDurSpin->setToolTip("0 = effetto permanente fino a Reset Effetti; > 0 = reset automatico dopo N secondi");
    m_effectFxBtn = new QPushButton("⚙ Catena Effetti...");
    m_effectFxBtn->setToolTip("Apri la catena di plugin da applicare al target");
    effectForm->addRow("Durata:", m_effectDurSpin);
    effectForm->addRow(m_effectFxBtn);
    ctrlLay->addWidget(m_effectGroup);
    propsLay->addWidget(m_controlSection);

    // ── Mic cue section ──────────────────────────────────────
    m_micSection = new QWidget;
    auto *micLay = new QVBoxLayout(m_micSection);
    micLay->setContentsMargins(0, 2, 0, 0);
    micLay->setSpacing(4);

    auto *micRow = new QWidget;
    auto *micRowLay = new QHBoxLayout(micRow);
    micRowLay->setContentsMargins(0, 0, 0, 0);
    micRowLay->setSpacing(6);

    auto *micGroup = new QGroupBox("Ingresso microfono");
    auto *micForm  = new QFormLayout(micGroup);
    micForm->setSpacing(3);
    m_micDeviceCombo = new QComboBox;
    m_micDeviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    micForm->addRow("Dispositivo:", m_micDeviceCombo);

    m_micVolumeSpin = new QDoubleSpinBox;
    m_micVolumeSpin->setRange(-60.0, 0.0);
    m_micVolumeSpin->setSingleStep(0.5);
    m_micVolumeSpin->setDecimals(1);
    m_micVolumeSpin->setSuffix(" dB");
    micForm->addRow("Volume:", m_micVolumeSpin);
    micRowLay->addWidget(micGroup);
    micRowLay->addStretch();
    micLay->addWidget(micRow);
    propsLay->addWidget(m_micSection);

    // ── Audio-only section: channel routing + waveform ───────
    m_audioSection = new QWidget;
    auto *audioLay = new QVBoxLayout(m_audioSection);
    audioLay->setContentsMargins(0, 2, 0, 0);
    audioLay->setSpacing(4);

    auto *chanRow = new QWidget;
    auto *chanLay = new QHBoxLayout(chanRow);
    chanLay->setContentsMargins(0, 0, 0, 0);
    chanLay->setSpacing(6);
    chanLay->addWidget(new QLabel("Uscita:"));
    m_channelCombo = new QComboBox;
    m_channelCombo->addItems({"L + R (stereo)", "Solo L", "Solo R"});
    m_channelCombo->setFixedWidth(140);
    chanLay->addWidget(m_channelCombo);
    chanLay->addStretch();
    audioLay->addWidget(chanRow);

    m_waveformView = new WaveformView;
    m_waveformView->setMinimumHeight(110);

    m_vuL = new VuMeter;
    m_vuR = new VuMeter;
    m_vuL->setToolTip("L");
    m_vuR->setToolTip("R");

    auto *waveRow = new QWidget;
    auto *waveRowLay = new QHBoxLayout(waveRow);
    waveRowLay->setContentsMargins(0, 0, 0, 0);
    waveRowLay->setSpacing(3);
    waveRowLay->addWidget(m_waveformView, 1);
    waveRowLay->addWidget(m_vuL);
    waveRowLay->addWidget(m_vuR);
    audioLay->addWidget(waveRow, 1);

    // Plugin chain widgets — each cue type gets its own instance so they never share state
    m_pluginChainWidget = new PluginChainWidget;
    connect(m_pluginChainWidget, &PluginChainWidget::chainModified,
            this, [this]() {
        if (m_cue) emit m_cue->propertyChanged();
    });

    m_effectPluginChainWidget = new PluginChainWidget;
    connect(m_effectPluginChainWidget, &PluginChainWidget::chainModified,
            this, [this]() {
        if (m_cue) emit m_cue->propertyChanged();
    });

    auto *fxRow    = new QWidget;
    auto *fxRowLay = new QHBoxLayout(fxRow);
    fxRowLay->setContentsMargins(0, 2, 0, 0);
    fxRowLay->setSpacing(6);
    m_fxBtn = new QPushButton("⚙ Effetti...");
    m_fxBtn->setToolTip("Apri catena effetti per questa cue");
    fxRowLay->addWidget(m_fxBtn);
    fxRowLay->addStretch();
    audioLay->addWidget(fxRow);

    connect(m_fxBtn, &QPushButton::clicked, this, [this]() {
        if (!m_fxDialog) {
            m_fxDialog = new QDialog(this, Qt::Window);
            m_fxDialog->setWindowTitle("Effetti — " + (m_cue ? m_cue->name() : QString()));
            m_fxDialog->setAttribute(Qt::WA_DeleteOnClose, false);
            m_fxDialog->resize(480, 400);
            auto *lay = new QVBoxLayout(m_fxDialog);
            lay->addWidget(m_pluginChainWidget);
        }
        m_fxDialog->setWindowTitle("Effetti — " + (m_cue ? m_cue->name() : QString()));
        m_fxDialog->show();
        m_fxDialog->raise();
        m_fxDialog->activateWindow();
    });

    // ── Slice / Rate section (audio only) ───────────────────
    m_sliceSection = new QWidget;
    auto *sliceLay = new QVBoxLayout(m_sliceSection);
    sliceLay->setContentsMargins(0, 4, 0, 0);
    sliceLay->setSpacing(4);

    // Rate control row
    auto *rateRow = new QWidget;
    auto *rateRowLay = new QHBoxLayout(rateRow);
    rateRowLay->setContentsMargins(0, 0, 0, 0);
    rateRowLay->setSpacing(6);
    rateRowLay->addWidget(new QLabel("Rate:"));
    m_rateSpin = new QDoubleSpinBox;
    m_rateSpin->setRange(0.1, 4.0);
    m_rateSpin->setSingleStep(0.05);
    m_rateSpin->setDecimals(2);
    m_rateSpin->setSuffix(" ×");
    m_rateSpin->setValue(1.0);
    m_rateSpin->setFixedWidth(80);
    m_rateSpin->setToolTip("Velocità di riproduzione (1.0 = normale)");
    rateRowLay->addWidget(m_rateSpin);
    auto *pitchCheck = new QCheckBox("Mantieni pitch");
    pitchCheck->setEnabled(false);
    pitchCheck->setToolTip("Richiede la libreria soundtouch (non disponibile)");
    rateRowLay->addWidget(pitchCheck);
    rateRowLay->addStretch();
    sliceLay->addWidget(rateRow);

    // Slice table header row
    auto *sliceHdrRow = new QWidget;
    auto *sliceHdrLay = new QHBoxLayout(sliceHdrRow);
    sliceHdrLay->setContentsMargins(0, 0, 0, 0);
    sliceHdrLay->setSpacing(4);
    sliceHdrLay->addWidget(new QLabel("Slices:"));
    m_addSliceBtn = new QPushButton("+ Aggiungi");
    m_addSliceBtn->setFixedHeight(22);
    m_addSliceBtn->setToolTip("Aggiunge una slice alla posizione corrente (Ctrl+Click sulla waveform per aggiungere)");
    m_clearSlicesBtn = new QPushButton("Rimuovi tutte");
    m_clearSlicesBtn->setFixedHeight(22);
    sliceHdrLay->addWidget(m_addSliceBtn);
    sliceHdrLay->addWidget(m_clearSlicesBtn);
    sliceHdrLay->addStretch();
    sliceLay->addWidget(sliceHdrRow);

    // Slice table: Seg # | Inizio | Loop
    m_sliceTable = new QTableWidget(0, 3);
    m_sliceTable->setHorizontalHeaderLabels({"Seg", "Inizio", "Loop"});
    m_sliceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sliceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_sliceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sliceTable->verticalHeader()->setVisible(false);
    m_sliceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sliceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sliceTable->setMaximumHeight(120);
    m_sliceTable->setStyleSheet("font-size:11px;");
    sliceLay->addWidget(m_sliceTable);
    audioLay->addWidget(m_sliceSection);

    propsLay->addWidget(m_audioSection, 1);

    // ── Text cue section ────────────────────────────────────
    m_textSection = new QWidget;
    auto *textLay = new QVBoxLayout(m_textSection);
    textLay->setContentsMargins(0, 2, 0, 0);
    textLay->setSpacing(4);

    auto *textRow = new QWidget;
    auto *textRowLay = new QHBoxLayout(textRow);
    textRowLay->setContentsMargins(0, 0, 0, 0);
    textRowLay->setSpacing(6);

    auto *textContentGroup = new QGroupBox("Testo");
    auto *textContentForm = new QFormLayout(textContentGroup);
    textContentForm->setSpacing(3);
    m_textContent = new QTextEdit;
    m_textContent->setMaximumHeight(70);
    m_textContent->setPlaceholderText("Inserisci il testo...");
    textContentForm->addRow(m_textContent);
    textRowLay->addWidget(textContentGroup, 2);

    auto *textFmtGroup = new QGroupBox("Formattazione");
    auto *textFmtForm = new QFormLayout(textFmtGroup);
    textFmtForm->setSpacing(3);

    m_fontFamilyCombo = new QFontComboBox;
    m_fontFamilyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    textFmtForm->addRow("Font:", m_fontFamilyCombo);

    m_fontSizeSpin = new QSpinBox;
    m_fontSizeSpin->setRange(6, 300);
    m_fontSizeSpin->setValue(48);
    m_fontSizeSpin->setSuffix(" pt");
    m_fontSizeSpin->setFixedWidth(80);
    textFmtForm->addRow("Dimensione:", m_fontSizeSpin);

    auto *styleRow = new QWidget;
    auto *styleRowLay = new QHBoxLayout(styleRow);
    styleRowLay->setContentsMargins(0, 0, 0, 0);
    styleRowLay->setSpacing(4);
    m_textBoldCheck   = new QCheckBox("Grassetto");
    m_textItalicCheck = new QCheckBox("Corsivo");
    styleRowLay->addWidget(m_textBoldCheck);
    styleRowLay->addWidget(m_textItalicCheck);
    styleRowLay->addStretch();
    textFmtForm->addRow(styleRow);

    m_textColorBtn = new QPushButton("Colore testo");
    m_textColorBtn->setFixedHeight(24);
    textFmtForm->addRow(m_textColorBtn);

    m_textBgColorBtn = new QPushButton("Colore sfondo");
    m_textBgColorBtn->setFixedHeight(24);
    textFmtForm->addRow(m_textBgColorBtn);

    m_textAlignCombo = new QComboBox;
    m_textAlignCombo->addItem("Sinistra",  int(Qt::AlignLeft  | Qt::AlignVCenter));
    m_textAlignCombo->addItem("Centro",    int(Qt::AlignCenter));
    m_textAlignCombo->addItem("Destra",    int(Qt::AlignRight | Qt::AlignVCenter));
    textFmtForm->addRow("Allineamento:", m_textAlignCombo);

    textRowLay->addWidget(textFmtGroup, 3);
    textLay->addWidget(textRow);
    propsLay->addWidget(m_textSection);

    // ── Script section ───────────────────────────────────────
    m_scriptSection = new QWidget;
    auto *scriptLay = new QVBoxLayout(m_scriptSection);
    scriptLay->setContentsMargins(0, 4, 0, 0);

    m_scriptRunBtn = new QPushButton("✎  Modifica Script…");
    scriptLay->addWidget(m_scriptRunBtn);

    propsLay->addWidget(m_scriptSection);

    connect(m_scriptRunBtn, &QPushButton::clicked, this, [this]() {
        auto *sc = qobject_cast<ScriptCue*>(m_cue);
        if (!sc) return;
        ScriptEditorDialog dlg(sc, this);
        dlg.exec();
    });

    // ── Playhead refresh timer ───────────────────────────────
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(80);
    connect(m_playTimer, &QTimer::timeout, this, &InspectorPanel::refreshPlayhead);

    // ── VU meter refresh timer ───────────────────────────────
    m_vuTimer = new QTimer(this);
    m_vuTimer->setInterval(50);
    connect(m_vuTimer, &QTimer::timeout, this, [this]() {
        m_vuL->setLevel(AudioEngine::instance().peakL());
        m_vuR->setLevel(AudioEngine::instance().peakR());
    });
    m_vuTimer->start();

    // ── Stack ────────────────────────────────────────────────
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_emptyWidget);  // index 0
    m_stack->addWidget(propsWidget);    // index 1
    outer->addWidget(m_stack);

    // ── Connections ──────────────────────────────────────────
    connect(m_numberEdit,       &QLineEdit::editingFinished,       this, &InspectorPanel::onNumberChanged);
    connect(m_nameEdit,         &QLineEdit::editingFinished,       this, &InspectorPanel::onNameChanged);
    connect(m_notesEdit,        &QTextEdit::textChanged,           this, &InspectorPanel::onNotesChanged);
    connect(m_preWaitSpin,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InspectorPanel::onPreWaitChanged);
    connect(m_postWaitSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InspectorPanel::onPostWaitChanged);
    connect(m_autoContinueCheck,&QCheckBox::toggled, this, &InspectorPanel::onAutoContinueChanged);
    connect(m_autoFollowCheck,  &QCheckBox::toggled, this, &InspectorPanel::onAutoFollowChanged);
    connect(m_browseBtn,        &QPushButton::clicked, this, &InspectorPanel::onBrowseFile);
    connect(m_volumeSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InspectorPanel::onVolumeChanged);
    connect(m_fadeInSpin,       QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InspectorPanel::onFadeInChanged);
    connect(m_fadeOutSpin,      QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &InspectorPanel::onFadeOutChanged);
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorPanel::onChannelChanged);
    connect(m_fadeInCheck,  &QCheckBox::toggled, this, &InspectorPanel::onFadeInCheckChanged);
    connect(m_fadeOutCheck, &QCheckBox::toggled, this, &InspectorPanel::onFadeOutCheckChanged);

    connect(m_waveformView, &WaveformView::trimStartChanged, this, [this](double secs) {
        if (m_cue && m_cue->type() == Cue::Type::Audio)
            static_cast<AudioCue*>(m_cue)->setTrimStart(secs);
    });
    connect(m_waveformView, &WaveformView::trimEndChanged, this, [this](double secs) {
        if (m_cue && m_cue->type() == Cue::Type::Audio)
            static_cast<AudioCue*>(m_cue)->setTrimEnd(secs);
    });
    connect(m_waveformView, &WaveformView::volumePointsChanged, this, [this](const QVector<QPointF> &pts) {
        if (m_cue && m_cue->type() == Cue::Type::Audio)
            static_cast<AudioCue*>(m_cue)->setVolumePoints(pts);
    });
    connect(m_waveformView, &WaveformView::fadeInChanged, this, [this](double s) {
        if (m_cue && m_cue->type() == Cue::Type::Audio) {
            blockSignals(true);
            m_fadeInSpin->setValue(s);
            blockSignals(false);
            static_cast<AudioCue*>(m_cue)->setFadeInDuration(s);
        }
    });
    connect(m_waveformView, &WaveformView::fadeOutChanged, this, [this](double s) {
        if (m_cue && m_cue->type() == Cue::Type::Audio) {
            blockSignals(true);
            m_fadeOutSpin->setValue(s);
            blockSignals(false);
            static_cast<AudioCue*>(m_cue)->setFadeOutDuration(s);
        }
    });

    // Slice markers ↔ AudioCue
    connect(m_waveformView, &WaveformView::sliceMarkersChanged,
            this, [this](const QVector<double> &normPos) {
        auto *a = qobject_cast<AudioCue*>(m_cue);
        if (!a || m_loadingFromCue) return;
        const double dur = a->duration();
        QVector<AudioSlice> existing = a->slices();

        // Keep 1 implicit first segment; sync divider markers (normPos) with slices[1:]
        QVector<AudioSlice> newSlices;
        // Segment 0 always starts at 0 with existing loopCount or 1
        AudioSlice seg0;
        seg0.posSec    = 0.0;
        seg0.loopCount = (!existing.isEmpty()) ? existing[0].loopCount : 1;
        newSlices.append(seg0);
        for (int i = 0; i < normPos.size(); ++i) {
            AudioSlice s;
            s.posSec = normPos[i] * dur;
            s.loopCount = (i + 1 < existing.size()) ? existing[i+1].loopCount : 1;
            newSlices.append(s);
        }
        a->setSlices(normPos.isEmpty() ? QVector<AudioSlice>{} : newSlices);
        rebuildSliceTable(a);
    });

    // Rate spin
    connect(m_rateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        auto *a = qobject_cast<AudioCue*>(m_cue);
        if (a && !m_loadingFromCue) a->setUserRate(v);
    });

    // Add/clear slice buttons
    connect(m_addSliceBtn, &QPushButton::clicked, this, [this]() {
        auto *a = qobject_cast<AudioCue*>(m_cue);
        if (!a || a->duration() < 0.1) return;
        // Add slice at current playback position or midpoint
        const double pos = (a->state() == Cue::State::Playing) ? a->position() : a->duration() / 2.0;
        const double normPos = qBound(0.001, pos / a->duration(), 0.999);
        QVector<double> markers = buildSliceMarkers(a);
        if (!markers.contains(normPos)) {
            markers.append(normPos);
            std::sort(markers.begin(), markers.end());
            m_waveformView->setSliceMarkers(markers);
            m_waveformView->sliceMarkersChanged(markers);
        }
    });
    connect(m_clearSlicesBtn, &QPushButton::clicked, this, [this]() {
        auto *a = qobject_cast<AudioCue*>(m_cue);
        if (!a) return;
        a->setSlices({});
        m_waveformView->setSliceMarkers({});
        rebuildSliceTable(a);
    });

    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorPanel::onTargetChanged);
    connect(m_fadeTargetVolSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &InspectorPanel::onFadeTargetVolChanged);
    connect(m_fadeDurationSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &InspectorPanel::onFadeDurationChanged);
    connect(m_fadeStopAtEndCheck, &QCheckBox::toggled,
            this, &InspectorPanel::onFadeStopAtEndChanged);
    connect(m_speedRateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &InspectorPanel::onSpeedRateChanged);
    connect(m_effectDurSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &InspectorPanel::onEffectDurationChanged);
    connect(m_effectFxBtn, &QPushButton::clicked, this, [this]() {
        if (!m_effectFxDialog) {
            m_effectFxDialog = new QDialog(this, Qt::Window);
            m_effectFxDialog->setAttribute(Qt::WA_DeleteOnClose, false);
            m_effectFxDialog->resize(480, 400);
            auto *lay = new QVBoxLayout(m_effectFxDialog);
            lay->addWidget(m_effectPluginChainWidget);
        }
        m_effectFxDialog->setWindowTitle("Effetti — " + (m_cue ? m_cue->name() : QString()));
        m_effectFxDialog->show();
        m_effectFxDialog->raise();
        m_effectFxDialog->activateWindow();
    });
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorPanel::onMicDeviceChanged);
    connect(m_micVolumeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &InspectorPanel::onMicVolumeChanged);

    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (!m_cue) return;
        if (m_cue->state() == Cue::State::Playing)
            m_cue->pause();
        else
            m_cue->go();  // handles both Idle and Paused
    });
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        if (m_cue) m_cue->stop();
    });
    connect(m_loopSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &InspectorPanel::onLoopCountChanged);

    connect(m_textContent, &QTextEdit::textChanged, this, &InspectorPanel::onTextContentChanged);
    connect(m_fontFamilyCombo, &QFontComboBox::currentFontChanged,
            this, &InspectorPanel::onFontFamilyChanged);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &InspectorPanel::onFontSizeChanged);
    connect(m_textBoldCheck,   &QCheckBox::toggled, this, &InspectorPanel::onBoldChanged);
    connect(m_textItalicCheck, &QCheckBox::toggled, this, &InspectorPanel::onItalicChanged);
    connect(m_textColorBtn,    &QPushButton::clicked, this, &InspectorPanel::onTextColorClicked);
    connect(m_textBgColorBtn,  &QPushButton::clicked, this, &InspectorPanel::onBgColorClicked);
    connect(m_textAlignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InspectorPanel::onTextAlignChanged);
}

void InspectorPanel::setCue(Cue *cue) {
    if (m_cue) {
        disconnect(m_cue, nullptr, this, nullptr);
        m_playTimer->stop();
    }

    m_cue = cue;

    if (!m_cue) {
        m_stack->setCurrentIndex(0);
        m_waveformView->setPlayPosition(0);
        m_pluginChainWidget->setChain(nullptr);
        m_effectPluginChainWidget->setChain(nullptr);
        return;
    }

    connect(m_cue, &Cue::propertyChanged, this, &InspectorPanel::onCuePropertyChanged);
    connect(m_cue, &Cue::stateChanged,    this, &InspectorPanel::onCueStateChanged);
    m_stack->setCurrentIndex(1);
    syncPlayButton(m_cue->state());
    if (m_cue->state() == Cue::State::Playing) m_playTimer->start();
    loadFromCue();
}

void InspectorPanel::loadFromCue() {
    if (!m_cue || m_loadingFromCue) return;
    m_loadingFromCue = true;
    blockSignals(true);

    m_typeLabel->setText(m_cue->typeName() + " Cue");
    m_numberEdit->setText(m_cue->number());
    m_nameEdit->setText(m_cue->name());
    m_notesEdit->setPlainText(m_cue->notes());
    m_preWaitSpin->setValue(m_cue->preWait());
    m_postWaitSpin->setValue(m_cue->postWait());
    m_autoContinueCheck->setChecked(m_cue->autoContinue());
    m_autoFollowCheck->setChecked(m_cue->autoFollow());

    updateMediaSection();
    blockSignals(false);
    m_loadingFromCue = false;
}

void InspectorPanel::updateMediaSection() {
    if (!m_cue) return;

    const Cue::Type t = m_cue->type();
    const bool isAudio   = (t == Cue::Type::Audio);
    const bool isVideo   = (t == Cue::Type::Video);
    const bool isEffect  = (t == Cue::Type::Effect);
    const bool isControl = (t == Cue::Type::Stop || t == Cue::Type::Fade
                         || t == Cue::Type::Pause || t == Cue::Type::Speed
                         || t == Cue::Type::Play  || isEffect
                         || t == Cue::Type::ResetEffect);
    const bool isSpeed   = (t == Cue::Type::Speed);
    const bool isMic     = (t == Cue::Type::Mic);
    const bool isTextCue   = (t == Cue::Type::Text);
    const bool isScriptCue = (t == Cue::Type::Script);

    m_mediaGroup->setVisible(isAudio || isVideo);
    m_fadeGroup->setVisible(isAudio);
    m_audioSection->setVisible(isAudio);
    m_controlSection->setVisible(isControl);
    m_micSection->setVisible(isMic);
    m_textSection->setVisible(isTextCue);
    m_scriptSection->setVisible(isScriptCue);

    if (isAudio) {
        auto *a = static_cast<AudioCue*>(m_cue);
        m_fileEdit->setText(QFileInfo(a->filePath()).fileName());
        m_volumeSpin->setValue(linearToDb(a->volume()));
        m_loopSpin->setValue(a->loopCount());
        m_fadeInSpin->setValue(a->fadeInDuration());
        m_fadeOutSpin->setValue(a->fadeOutDuration());
        m_channelCombo->setCurrentIndex(int(a->channelRoute()));

        m_fadeInCheck->setChecked(a->fadeInDuration() > 0.01);
        m_fadeOutCheck->setChecked(a->fadeOutDuration() > 0.01);
        m_fadeInSpin->setEnabled(a->fadeInDuration() > 0.01);
        m_fadeOutSpin->setEnabled(a->fadeOutDuration() > 0.01);

        m_waveformView->setFilePath(a->filePath());
        m_waveformView->setDuration(a->duration());
        m_waveformView->setFadeIn(a->fadeInDuration());
        m_waveformView->setFadeOut(a->fadeOutDuration());
        m_waveformView->setVolumePoints(a->volumePoints());
        m_waveformView->setTrimStart(a->trimStart());
        m_waveformView->setTrimEnd(a->trimEnd());
        m_waveformView->setSliceMarkers(buildSliceMarkers(a));
        m_rateSpin->blockSignals(true);
        m_rateSpin->setValue(a->userRate());
        m_rateSpin->blockSignals(false);
        rebuildSliceTable(a);
        m_pluginChainWidget->setChain(a->pluginChain());
        m_effectPluginChainWidget->setChain(nullptr);
    } else if (isVideo) {
        auto *v = static_cast<VideoCue*>(m_cue);
        m_fileEdit->setText(QFileInfo(v->filePath()).fileName());
        m_volumeSpin->setValue(linearToDb(v->volume()));
        m_loopSpin->setValue(v->loopCount());
        m_pluginChainWidget->setChain(nullptr);
        m_effectPluginChainWidget->setChain(nullptr);
    } else if (isMic) {
        auto *mc = static_cast<MicCue*>(m_cue);
        m_micDeviceCombo->blockSignals(true);
        m_micDeviceCombo->clear();
        m_micDeviceCombo->addItem("Default", QString{});
        for (const auto &dev : QMediaDevices::audioInputs())
            m_micDeviceCombo->addItem(dev.description(), dev.id());
        const int devIdx = m_micDeviceCombo->findData(mc->inputDeviceId());
        m_micDeviceCombo->setCurrentIndex(devIdx >= 0 ? devIdx : 0);
        m_micDeviceCombo->blockSignals(false);
        m_micVolumeSpin->blockSignals(true);
        m_micVolumeSpin->setValue(linearToDb(mc->volume()));
        m_micVolumeSpin->blockSignals(false);
        m_pluginChainWidget->setChain(nullptr);
        m_effectPluginChainWidget->setChain(nullptr);
    } else if (isControl) {
        populateTargetCombo();
        auto *cc = static_cast<ControlCue*>(m_cue);
        const int idx = m_targetCombo->findData(cc->targetId());
        m_targetCombo->blockSignals(true);
        m_targetCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_targetCombo->blockSignals(false);

        const bool isFade = (t == Cue::Type::Fade);
        m_fadeParamsGroup->setVisible(isFade);
        m_speedGroup->setVisible(isSpeed);
        m_effectGroup->setVisible(isEffect);
        if (isEffect) {
            auto *ec = static_cast<EffectCue*>(m_cue);
            m_effectDurSpin->blockSignals(true);
            m_effectDurSpin->setValue(ec->effectDuration());
            m_effectDurSpin->blockSignals(false);
            m_effectPluginChainWidget->setChain(ec->pluginChain());
            m_pluginChainWidget->setChain(nullptr);
        } else {
            m_pluginChainWidget->setChain(nullptr);
            m_effectPluginChainWidget->setChain(nullptr);
        }
        if (isSpeed) {
            auto *sc = static_cast<SpeedCue*>(m_cue);
            m_speedRateSpin->blockSignals(true);
            m_speedRateSpin->setValue(sc->rate());
            m_speedRateSpin->blockSignals(false);
        }
        if (isFade) {
            auto *fc = static_cast<FadeCue*>(m_cue);
            m_fadeTargetVolSpin->blockSignals(true);
            m_fadeDurationSpin->blockSignals(true);
            m_fadeStopAtEndCheck->blockSignals(true);
            m_fadeTargetVolSpin->setValue(linearToDb(fc->targetVolume()));
            m_fadeDurationSpin->setValue(fc->fadeDuration());
            m_fadeStopAtEndCheck->setChecked(fc->stopAtEnd());
            m_fadeTargetVolSpin->blockSignals(false);
            m_fadeDurationSpin->blockSignals(false);
            m_fadeStopAtEndCheck->blockSignals(false);
        }
    } else if (isTextCue) {
        auto *tc = static_cast<TextCue*>(m_cue);
        m_textContent->blockSignals(true);
        m_fontFamilyCombo->blockSignals(true);
        m_fontSizeSpin->blockSignals(true);
        m_textBoldCheck->blockSignals(true);
        m_textItalicCheck->blockSignals(true);
        m_textAlignCombo->blockSignals(true);

        m_textContent->setPlainText(tc->text());
        m_fontFamilyCombo->setCurrentFont(QFont(tc->fontFamily()));
        m_fontSizeSpin->setValue(tc->fontSize());
        m_textBoldCheck->setChecked(tc->bold());
        m_textItalicCheck->setChecked(tc->italic());

        auto colorBtnStyle = [](const QColor &c) {
            return QString("background:%1; color:%2;")
                .arg(c.name(), c.lightness() > 128 ? "#000" : "#fff");
        };
        m_textColorBtn->setStyleSheet(colorBtnStyle(tc->textColor()));
        m_textBgColorBtn->setStyleSheet(colorBtnStyle(tc->backgroundColor()));

        // Find matching alignment index
        const int alignVal = tc->alignment();
        for (int i = 0; i < m_textAlignCombo->count(); ++i) {
            if (m_textAlignCombo->itemData(i).toInt() == alignVal) {
                m_textAlignCombo->setCurrentIndex(i);
                break;
            }
        }

        m_textContent->blockSignals(false);
        m_fontFamilyCombo->blockSignals(false);
        m_fontSizeSpin->blockSignals(false);
        m_textBoldCheck->blockSignals(false);
        m_textItalicCheck->blockSignals(false);
        m_textAlignCombo->blockSignals(false);
        m_pluginChainWidget->setChain(nullptr);
        m_effectPluginChainWidget->setChain(nullptr);
    }
}

void InspectorPanel::blockSignals(bool block) {
    m_numberEdit->blockSignals(block);
    m_nameEdit->blockSignals(block);
    m_notesEdit->blockSignals(block);
    m_preWaitSpin->blockSignals(block);
    m_postWaitSpin->blockSignals(block);
    m_autoContinueCheck->blockSignals(block);
    m_autoFollowCheck->blockSignals(block);
    m_volumeSpin->blockSignals(block);
    m_loopSpin->blockSignals(block);
    m_fadeInCheck->blockSignals(block);
    m_fadeInSpin->blockSignals(block);
    m_fadeOutCheck->blockSignals(block);
    m_fadeOutSpin->blockSignals(block);
    m_channelCombo->blockSignals(block);
    if (m_fadeStopAtEndCheck) m_fadeStopAtEndCheck->blockSignals(block);
    if (m_effectDurSpin)      m_effectDurSpin->blockSignals(block);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void InspectorPanel::onNumberChanged() {
    if (m_cue) m_cue->setNumber(m_numberEdit->text().trimmed());
}

void InspectorPanel::onNameChanged() {
    if (m_cue) m_cue->setName(m_nameEdit->text().trimmed());
}

void InspectorPanel::onNotesChanged() {
    if (m_cue) m_cue->setNotes(m_notesEdit->toPlainText());
}

void InspectorPanel::onPreWaitChanged(double v) {
    if (m_cue) m_cue->setPreWait(v);
}

void InspectorPanel::onPostWaitChanged(double v) {
    if (m_cue) m_cue->setPostWait(v);
}

void InspectorPanel::onAutoContinueChanged(bool v) {
    if (m_cue) {
        m_cue->setAutoContinue(v);
        if (v) { m_autoFollowCheck->blockSignals(true); m_autoFollowCheck->setChecked(false); m_autoFollowCheck->blockSignals(false); m_cue->setAutoFollow(false); }
    }
}

void InspectorPanel::onAutoFollowChanged(bool v) {
    if (m_cue) {
        m_cue->setAutoFollow(v);
        if (v) { m_autoContinueCheck->blockSignals(true); m_autoContinueCheck->setChecked(false); m_autoContinueCheck->blockSignals(false); m_cue->setAutoContinue(false); }
    }
}

void InspectorPanel::onBrowseFile() {
    if (!m_cue) return;
    const bool isAudio = (m_cue->type() == Cue::Type::Audio);
    const QString filter = isAudio
        ? "File audio (*.mp3 *.wav *.flac *.ogg *.aac *.m4a *.opus *.aiff *.aif)"
        : "File video (*.mp4 *.mkv *.avi *.mov *.webm *.m4v)";
    const QString path = QFileDialog::getOpenFileName(this, "Scegli file", {}, filter);
    if (path.isEmpty()) return;
    // Deferred timers (e.g. group collapse) can fire inside the dialog's nested event loop,
    // resetting the model and clearing the selection → setCue(nullptr). Re-check before use.
    if (!m_cue) return;
    const Cue::Type expectedType = isAudio ? Cue::Type::Audio : Cue::Type::Video;
    if (m_cue->type() != expectedType) return;
    if (isAudio) static_cast<AudioCue*>(m_cue)->setFilePath(path);
    else         static_cast<VideoCue*>(m_cue)->setFilePath(path);
    updateMediaSection();
    loadFromCue();
}

void InspectorPanel::onFilePathChanged() {}

void InspectorPanel::onVolumeChanged(double v) {
    if (!m_cue) return;
    const double linear = dbToLinear(v);
    if (m_cue->type() == Cue::Type::Audio) static_cast<AudioCue*>(m_cue)->setVolume(linear);
    if (m_cue->type() == Cue::Type::Video) static_cast<VideoCue*>(m_cue)->setVolume(linear);
}

void InspectorPanel::onFadeInChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Audio) {
        static_cast<AudioCue*>(m_cue)->setFadeInDuration(v);
        m_waveformView->setFadeIn(v);
    }
}

void InspectorPanel::onFadeOutChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Audio) {
        static_cast<AudioCue*>(m_cue)->setFadeOutDuration(v);
        m_waveformView->setFadeOut(v);
    }
}

void InspectorPanel::onFadeInCheckChanged(bool checked) {
    if (!m_cue || m_cue->type() != Cue::Type::Audio) return;
    m_fadeInSpin->setEnabled(checked);
    const double val = checked ? (m_fadeInSpin->value() > 0.001 ? m_fadeInSpin->value() : 3.0) : 0.0;
    if (checked && m_fadeInSpin->value() < 0.001) {
        m_fadeInSpin->blockSignals(true);
        m_fadeInSpin->setValue(3.0);
        m_fadeInSpin->blockSignals(false);
    }
    static_cast<AudioCue*>(m_cue)->setFadeInDuration(val);
    m_waveformView->setFadeIn(val);
}

void InspectorPanel::onFadeOutCheckChanged(bool checked) {
    if (!m_cue || m_cue->type() != Cue::Type::Audio) return;
    m_fadeOutSpin->setEnabled(checked);
    const double val = checked ? (m_fadeOutSpin->value() > 0.001 ? m_fadeOutSpin->value() : 3.0) : 0.0;
    if (checked && m_fadeOutSpin->value() < 0.001) {
        m_fadeOutSpin->blockSignals(true);
        m_fadeOutSpin->setValue(3.0);
        m_fadeOutSpin->blockSignals(false);
    }
    static_cast<AudioCue*>(m_cue)->setFadeOutDuration(val);
    m_waveformView->setFadeOut(val);
}

void InspectorPanel::onCuePropertyChanged() {
    loadFromCue();
}

void InspectorPanel::onChannelChanged(int idx) {
    if (m_cue && m_cue->type() == Cue::Type::Audio)
        static_cast<AudioCue*>(m_cue)->setChannelRoute(AudioCue::ChannelRoute(idx));
}

void InspectorPanel::syncPlayButton(Cue::State state) {
    switch (state) {
    case Cue::State::Playing: m_playBtn->setText("⏸  Pausa");   break;
    case Cue::State::Paused:  m_playBtn->setText("▶  Riprendi"); break;
    default:                  m_playBtn->setText("▶  Play");     break;
    }
}

void InspectorPanel::onCueStateChanged(Cue::State state) {
    syncPlayButton(state);
    switch (state) {
    case Cue::State::Playing:
        m_playTimer->start();
        break;
    case Cue::State::Paused:
        m_playTimer->stop();
        // leave cursor at current position
        break;
    default:
        m_playTimer->stop();
        m_waveformView->setPlayPosition(0);
        break;
    }
}

void InspectorPanel::refreshPlayhead() {
    if (m_cue)
        m_waveformView->setPlayPosition(m_cue->position());
}

void InspectorPanel::populateTargetCombo() {
    if (!m_cueList || !m_targetCombo) return;

    // PlayCue and SpeedCue can only target Audio or Video cues
    const bool mediaOnly = m_cue && (m_cue->type() == Cue::Type::Play
                                  || m_cue->type() == Cue::Type::Speed);
    // EffectCue and ResetEffectCue can only target AudioCue
    const bool audioOnly = m_cue && (m_cue->type() == Cue::Type::Effect
                                  || m_cue->type() == Cue::Type::ResetEffect);

    const QString currentId = m_targetCombo->currentData().toString();
    m_targetCombo->blockSignals(true);
    m_targetCombo->clear();
    m_targetCombo->addItem("(nessuno)", QString{});
    for (int i = 0; i < m_cueList->count(); ++i) {
        Cue *c = m_cueList->cueAt(i);
        if (c == m_cue) continue;
        if (mediaOnly && c->type() != Cue::Type::Audio && c->type() != Cue::Type::Video)
            continue;
        if (audioOnly && c->type() != Cue::Type::Audio)
            continue;
        const QString num = c->number().isEmpty() ? QString::number(i + 1) : c->number();
        const QString name = c->name().isEmpty() ? "(senza nome)" : c->name();
        m_targetCombo->addItem(num + " - " + name, c->id());
    }
    const int idx = m_targetCombo->findData(currentId);
    m_targetCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_targetCombo->blockSignals(false);
}

void InspectorPanel::onTargetChanged(int) {
    if (!m_cue) return;
    const Cue::Type t = m_cue->type();
    if (t == Cue::Type::Stop  || t == Cue::Type::Fade  || t == Cue::Type::Pause
     || t == Cue::Type::Play  || t == Cue::Type::Speed
     || t == Cue::Type::Effect || t == Cue::Type::ResetEffect)
        static_cast<ControlCue*>(m_cue)->setTargetId(m_targetCombo->currentData().toString());
}

void InspectorPanel::onFadeTargetVolChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Fade)
        static_cast<FadeCue*>(m_cue)->setTargetVolume(dbToLinear(v));
}

void InspectorPanel::onFadeDurationChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Fade)
        static_cast<FadeCue*>(m_cue)->setFadeDuration(v);
}

void InspectorPanel::onFadeStopAtEndChanged(bool v) {
    if (m_cue && m_cue->type() == Cue::Type::Fade)
        static_cast<FadeCue*>(m_cue)->setStopAtEnd(v);
}

void InspectorPanel::onMicDeviceChanged(int) {
    if (m_cue && m_cue->type() == Cue::Type::Mic)
        static_cast<MicCue*>(m_cue)->setInputDeviceId(m_micDeviceCombo->currentData().toString());
}

void InspectorPanel::onMicVolumeChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Mic)
        static_cast<MicCue*>(m_cue)->setVolume(dbToLinear(v));
}

void InspectorPanel::onEffectDurationChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Effect)
        static_cast<EffectCue*>(m_cue)->setEffectDuration(v);
}

void InspectorPanel::onSpeedRateChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Speed)
        static_cast<SpeedCue*>(m_cue)->setRate(v);
}

void InspectorPanel::onLoopCountChanged(int v) {
    if (!m_cue) return;
    if (m_cue->type() == Cue::Type::Audio)
        static_cast<AudioCue*>(m_cue)->setLoopCount(v);
    else if (m_cue->type() == Cue::Type::Video)
        static_cast<VideoCue*>(m_cue)->setLoopCount(v);
}

void InspectorPanel::onTextContentChanged() {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setText(m_textContent->toPlainText());
}

void InspectorPanel::onFontFamilyChanged(const QFont &f) {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setFontFamily(f.family());
}

void InspectorPanel::onFontSizeChanged(int v) {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setFontSize(v);
}

void InspectorPanel::onBoldChanged(bool v) {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setBold(v);
}

void InspectorPanel::onItalicChanged(bool v) {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setItalic(v);
}

void InspectorPanel::onTextColorClicked() {
    if (!m_cue || m_cue->type() != Cue::Type::Text) return;
    auto *tc = static_cast<TextCue*>(m_cue);
    const QColor c = QColorDialog::getColor(tc->textColor(), this, "Colore testo");
    if (!c.isValid()) return;
    tc->setTextColor(c);
    m_textColorBtn->setStyleSheet(
        QString("background:%1; color:%2;").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
}

void InspectorPanel::onBgColorClicked() {
    if (!m_cue || m_cue->type() != Cue::Type::Text) return;
    auto *tc = static_cast<TextCue*>(m_cue);
    const QColor c = QColorDialog::getColor(tc->backgroundColor(), this, "Colore sfondo");
    if (!c.isValid()) return;
    tc->setBackgroundColor(c);
    m_textBgColorBtn->setStyleSheet(
        QString("background:%1; color:%2;").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
}

void InspectorPanel::onTextAlignChanged(int idx) {
    if (m_cue && m_cue->type() == Cue::Type::Text)
        static_cast<TextCue*>(m_cue)->setAlignment(m_textAlignCombo->itemData(idx).toInt());
}

// ── Slice helpers ─────────────────────────────────────────────────────────────

QVector<double> InspectorPanel::buildSliceMarkers(AudioCue *a) const {
    if (!a || a->duration() < 0.01) return {};
    QVector<double> markers;
    const auto &slices = a->slices();
    for (int i = 1; i < slices.size(); ++i)   // skip segment 0 (starts at 0)
        markers.append(slices[i].posSec / a->duration());
    return markers;
}

void InspectorPanel::rebuildSliceTable(AudioCue *a) {
    if (!m_sliceTable || !a) return;
    const auto &slices = a->slices();
    const double dur = a->duration();

    m_sliceTable->blockSignals(true);
    m_sliceTable->setRowCount(0);

    if (slices.isEmpty()) {
        m_sliceTable->setVisible(false);
        m_clearSlicesBtn->setEnabled(false);
        m_sliceTable->blockSignals(false);
        return;
    }

    m_sliceTable->setVisible(true);
    m_clearSlicesBtn->setEnabled(true);

    for (int i = 0; i < slices.size(); ++i) {
        m_sliceTable->insertRow(i);
        m_sliceTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        const QString pos = (i == 0)
            ? "Inizio"
            : QString("%1 s").arg(slices[i].posSec, 0, 'f', 2);
        m_sliceTable->setItem(i, 1, new QTableWidgetItem(pos));

        auto *spin = new QSpinBox;
        spin->setRange(0, 99);
        spin->setValue(slices[i].loopCount);
        spin->setSpecialValueText("skip");
        spin->setToolTip("0 = salta, 1 = riproduci una volta, N = ripeti N volte");
        spin->setFrame(false);
        const int row = i;
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, row](int v) {
            auto *a = qobject_cast<AudioCue*>(m_cue);
            if (!a || m_loadingFromCue) return;
            auto slices2 = a->slices();
            if (row < slices2.size()) {
                slices2[row].loopCount = v;
                a->setSlices(slices2);
            }
        });
        m_sliceTable->setCellWidget(i, 2, spin);
    }
    m_sliceTable->blockSignals(false);
    Q_UNUSED(dur);
}
