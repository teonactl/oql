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
#include "engine/RecordCue.h"
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
#include <QScrollArea>
#include <QGuiApplication>
#include <QScreen>

// ── VU meter widget ───────────────────────────────────────────────────────────

class VuMeter : public QWidget {
    static constexpr int kBarW = 14;
    static constexpr int kGapW = 2;
    static constexpr int kSclW = 24;  // tick + label area
public:
    explicit VuMeter(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(kBarW + kGapW + kSclW);
        setMinimumHeight(56);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    void setLevel(float linear) {
        const float db = (linear <= 1e-7f) ? -60.0f
                       : std::max(-60.0f, 20.0f * std::log10(linear));
        m_env = (db > m_env) ? db : m_env + (db - m_env) * 0.25f;
        if (db >= m_peak) { m_peak = db; m_holdTicks = 50; }
        else if (m_holdTicks > 0) --m_holdTicks;
        else if (m_peak > -60.0f) m_peak -= 0.8f;
        update();
    }
    float currentDb() const { return m_env; }
private:
    float m_env   = -60.0f;
    float m_peak  = -60.0f;
    int   m_holdTicks = 0;

    static float frac(float db) { return (db + 60.0f) / 66.0f; }
    static int   dbY(float db, int H) {
        return int((1.0f - std::clamp(frac(db), 0.0f, 1.0f)) * float(H - 1));
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const int H = height();
        p.fillRect(0, 0, width(), H, QColor(18, 18, 18));

        // ── bar ──────────────────────────────────────────────
        const float f = std::clamp(frac(m_env), 0.0f, 1.0f);
        if (f >= 0.001f) {
            const int bY     = H - int(f * H);
            const int yRed   = H - int(frac(-6.0f)  * H);
            const int yYel   = H - int(frac(-18.0f) * H);
            if (bY < yRed)   p.fillRect(0, bY,               kBarW, yRed - bY,   QColor(220, 40,  40));
            const int yT = std::max(bY, yRed);
            if (yT < yYel)   p.fillRect(0, yT,               kBarW, yYel - yT,   QColor(200, 170, 20));
            const int gT = std::max(bY, yYel);
            if (gT < H)      p.fillRect(0, gT,               kBarW, H    - gT,   QColor(40,  180, 40));
        }
        // peak hold
        const float fp = std::clamp(frac(m_peak), 0.0f, 1.0f);
        if (fp > 0.01f) {
            const int pY = std::clamp(H - int(fp * H) - 1, 0, H - 2);
            p.fillRect(0, pY, kBarW, 2, fp > frac(-6.0f) ? QColor(255,100,100) : QColor(220,220,60));
        }

        // ── scale ─────────────────────────────────────────────
        // separator
        p.setPen(QColor(45, 45, 45));
        p.drawLine(kBarW, 0, kBarW, H - 1);

        const int sX = kBarW + kGapW;
        QFont scaleFont;
        scaleFont.setFamily("monospace");
        scaleFont.setPixelSize(8);
        p.setFont(scaleFont);

        // ticks every 6 dB; labels every 12 dB
        static const int kDbs[] = {0, -6, -12, -18, -24, -30, -36, -42, -48, -54};
        for (int db : kDbs) {
            const int y = dbY(float(db), H);
            const bool major = (db % 12 == 0);
            const int  tLen  = major ? 5 : 3;
            p.setPen(QColor(major ? 155 : 75, major ? 155 : 75, major ? 155 : 75));
            p.drawLine(sX, y, sX + tLen - 1, y);
            if (major) {
                p.setPen(QColor(170, 170, 170));
                const QString lbl = (db == 0) ? " 0" : QString::number(db);
                p.drawText(QRect(sX + tLen + 1, y - 4, kSclW - tLen - 1, 9),
                           Qt::AlignLeft | Qt::AlignVCenter, lbl);
            }
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
static QString dbLabel(float db) {
    return db <= -59.5f ? "-∞" : QString::number(int(db));
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

        // Refresh AudioCue source label when any RecordCue changes its linked target
        connect(m_cueList, &CueList::cuePropertyChanged, this, [this](int idx) {
            if (!m_cue || m_cue->type() != Cue::Type::Audio) return;
            if (m_cueList->cueAt(idx) == m_cue) return;
            if (qobject_cast<RecordCue*>(m_cueList->cueAt(idx)))
                updateMediaSection();
        });
        connect(m_cueList, &CueList::cueAdded,   this, [this](int) {
            if (m_cue && m_cue->type() == Cue::Type::Audio) updateMediaSection();
        });
        connect(m_cueList, &CueList::cueRemoved, this, [this](int) {
            if (m_cue && m_cue->type() == Cue::Type::Audio) updateMediaSection();
        });
    }
}

void InspectorPanel::buildUi() {
    setMinimumHeight(150);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);

    // ── Empty state ──────────────────────────────────────────
    m_emptyWidget = new QWidget;
    auto *emptyLay = new QHBoxLayout(m_emptyWidget);
    m_emptyLabel = new QLabel(tr("Seleziona una cue per vedere le proprietà"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #888; font-style: italic;");
    emptyLay->addWidget(m_emptyLabel);

    // ── Properties widget ────────────────────────────────────
    auto *propsWidget = new QWidget;
    auto *propsLay    = new QVBoxLayout(propsWidget);
    propsLay->setContentsMargins(0, 0, 0, 0);
    propsLay->setSpacing(2);

    // Header row: type label + (audio) channel routing + stop + play/pause
    m_headerRow = new QWidget;
    auto *headerLay = new QHBoxLayout(m_headerRow);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(5);
    m_typeLabel = new QLabel;
    m_typeLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #555;");
    m_stopBtn = new QPushButton("■");
    m_stopBtn->setFixedSize(28, 24);
    m_stopBtn->setToolTip(tr("Stop"));
    m_stopBtn->setStyleSheet("padding:1px 2px; color:#e07070; font-size:13px;");
    m_playBtn = new QPushButton("▶  " + tr("Play"));
    m_playBtn->setFixedSize(90, 24);
    m_playBtn->setToolTip(tr("Play / Pausa / Riprendi"));

    m_uscitaLabel = new QLabel(tr("Out:"));
    m_uscitaLabel->setStyleSheet("color:#8892a4; font-size:10px;");
    m_uscitaLabel->setVisible(false);
    m_channelCombo = new QComboBox;
    m_channelCombo->addItems({tr("L + R (stereo)"), tr("Solo L"), tr("Solo R")});
    m_channelCombo->setFixedWidth(112);
    m_channelCombo->setMaximumHeight(24);
    m_channelCombo->setVisible(false);

    headerLay->addWidget(m_typeLabel);
    headerLay->addStretch();
    headerLay->addWidget(m_uscitaLabel);
    headerLay->addWidget(m_channelCombo);
    headerLay->addWidget(m_stopBtn);
    headerLay->addWidget(m_playBtn);
    propsLay->addWidget(m_headerRow);

    // ── Groups row (horizontal) ──────────────────────────────
    m_groupsRow = new QWidget;
    auto *groupsLay = new QHBoxLayout(m_groupsRow);
    groupsLay->setContentsMargins(0, 0, 0, 0);
    groupsLay->setSpacing(4);

    auto makeSpin = [](double max = 999.0) {
        auto *s = new QDoubleSpinBox;
        s->setRange(0, max);
        s->setSingleStep(0.1);
        s->setDecimals(2);
        s->setSuffix(" s");
        return s;
    };

    // General group
    m_genGroup = new QGroupBox(tr("Generale"));
    m_genGroup->setMinimumWidth(185);
    m_genForm  = new QFormLayout(m_genGroup);
    m_genForm->setSpacing(3);

    m_numberEdit = new QLineEdit;
    m_numberEdit->setMaximumWidth(80);
    m_nameEdit   = new QLineEdit;
    m_notesEdit  = new QTextEdit;
    m_notesEdit->setMaximumHeight(28);
    m_notesEdit->setPlaceholderText(tr("Note..."));

    m_genForm->addRow(tr("Numero:"), m_numberEdit);
    m_genForm->addRow(tr("Nome:"),   m_nameEdit);
    m_genForm->addRow(tr("Note:"),   m_notesEdit);
    groupsLay->addWidget(m_genGroup);

    // Timing group
    m_timeGroup = new QGroupBox(tr("Timing"));
    m_timeGroup->setMinimumWidth(195);
    m_timeForm  = new QFormLayout(m_timeGroup);
    m_timeForm->setSpacing(3);

    m_preWaitSpin  = makeSpin();
    m_postWaitSpin = makeSpin();
    m_autoContinueCheck = new QCheckBox(tr("Auto-continue"));
    m_autoFollowCheck   = new QCheckBox(tr("Auto-follow"));

    m_timeForm->addRow(tr("Pre-wait:"),  m_preWaitSpin);
    m_timeForm->addRow(tr("Post-wait:"), m_postWaitSpin);
    m_timeForm->addRow(m_autoContinueCheck);
    m_timeForm->addRow(m_autoFollowCheck);
    groupsLay->addWidget(m_timeGroup);

    // Media group (audio + video)
    m_mediaGroup = new QGroupBox(tr("File sorgente"));
    m_mediaGroup->setMinimumWidth(220);
    m_mediaForm = new QFormLayout(m_mediaGroup);
    m_mediaForm->setSpacing(3);

    auto *fileRow = new QWidget;
    auto *fileRLay = new QHBoxLayout(fileRow);
    fileRLay->setContentsMargins(0, 0, 0, 0);
    m_fileEdit  = new QLineEdit;
    m_fileEdit->setReadOnly(true);
    m_fileEdit->setPlaceholderText(tr("Nessun file..."));
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
    m_loopSpin->setToolTip(tr("Ripetizioni: 0 = loop infinito, 1 = una volta"));
    m_loopSpin->setFixedWidth(60);

    m_mediaForm->addRow(tr("File:"), fileRow);
    m_mediaForm->addRow(tr("Volume:"), m_volumeSpin);
    m_mediaForm->addRow(tr("Loop:"), m_loopSpin);
    groupsLay->addWidget(m_mediaGroup);

    // Fade group (audio only) — with enable/disable checkboxes
    m_fadeGroup = new QGroupBox(tr("Fade"));
    m_fadeGroup->setMinimumWidth(175);
    m_fadeForm = new QFormLayout(m_fadeGroup);
    m_fadeForm->setSpacing(3);

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
        m_fadeForm->addRow(label, row);
    };

    makeFadeRow(m_fadeInCheck,  m_fadeInSpin,  tr("In:"));
    makeFadeRow(m_fadeOutCheck, m_fadeOutSpin, tr("Out:"));
    groupsLay->addWidget(m_fadeGroup);

    // ── Audio extras panel (right of group boxes): Effects, Rate, Slices controls ──
    m_audioExtrasPanel = new QWidget;
    m_audioExtrasPanel->setVisible(false);
    auto *extrasLay = new QVBoxLayout(m_audioExtrasPanel);
    extrasLay->setContentsMargins(4, 0, 0, 0);
    extrasLay->setSpacing(4);

    m_fxRow = new QWidget;
    auto *fxRowLay = new QHBoxLayout(m_fxRow);
    fxRowLay->setContentsMargins(0, 0, 0, 0);
    m_fxBtn = new QPushButton(tr("⚙ Effetti..."));
    m_fxBtn->setToolTip(tr("Apri catena effetti per questa cue"));
    fxRowLay->addWidget(m_fxBtn);
    extrasLay->addWidget(m_fxRow);

    auto *exRateRow = new QWidget;
    auto *exRateRowLay = new QHBoxLayout(exRateRow);
    exRateRowLay->setContentsMargins(0, 0, 0, 0);
    exRateRowLay->setSpacing(5);
    m_rateLabel = new QLabel(tr("Rate:"));
    exRateRowLay->addWidget(m_rateLabel);
    m_rateSpin = new QDoubleSpinBox;
    m_rateSpin->setRange(0.1, 4.0);
    m_rateSpin->setSingleStep(0.05);
    m_rateSpin->setDecimals(2);
    m_rateSpin->setSuffix(" ×");
    m_rateSpin->setValue(1.0);
    m_rateSpin->setFixedWidth(76);
    m_rateSpin->setToolTip(tr("Velocità di riproduzione (1.0 = normale)"));
    exRateRowLay->addWidget(m_rateSpin);
    m_pitchCheck = new QCheckBox(tr("Pitch"));
#ifdef HAVE_SOUNDTOUCH
    m_pitchCheck->setToolTip(tr("Time-stretch: mantieni la tonalità originale variando il rate"));
#else
    m_pitchCheck->setEnabled(false);
    m_pitchCheck->setToolTip(tr("Richiede la libreria soundtouch (non disponibile)"));
#endif
    exRateRowLay->addWidget(m_pitchCheck);
    extrasLay->addWidget(exRateRow);

    auto *exSliceHdrRow = new QWidget;
    auto *exSliceHdrLay = new QHBoxLayout(exSliceHdrRow);
    exSliceHdrLay->setContentsMargins(0, 0, 0, 0);
    exSliceHdrLay->setSpacing(4);
    m_slicesLabel = new QLabel(tr("Slices:"));
    exSliceHdrLay->addWidget(m_slicesLabel);
    m_addSliceBtn = new QPushButton("+");
    m_addSliceBtn->setFixedHeight(22);
    m_addSliceBtn->setFixedWidth(28);
    m_addSliceBtn->setToolTip(tr("Aggiunge una slice alla posizione corrente"));
    m_addSliceBtn->setStyleSheet("padding:1px 2px; font-weight:bold; font-size:13px;");
    m_clearSlicesBtn = new QPushButton(tr("✕ tutte"));
    m_clearSlicesBtn->setFixedHeight(22);
    exSliceHdrLay->addWidget(m_addSliceBtn);
    exSliceHdrLay->addWidget(m_clearSlicesBtn);
    extrasLay->addWidget(exSliceHdrRow);
    extrasLay->addStretch();

    groupsLay->addWidget(m_audioExtrasPanel);
    propsLay->addWidget(m_groupsRow);

    // ── Control cue section (Stop / Fade / Pause) ────────────
    m_controlSection = new QWidget;
    auto *ctrlLay = new QVBoxLayout(m_controlSection);
    ctrlLay->setContentsMargins(0, 2, 0, 0);
    ctrlLay->setSpacing(4);

    auto *ctrlRow = new QWidget;
    auto *ctrlRowLay = new QHBoxLayout(ctrlRow);
    ctrlRowLay->setContentsMargins(0, 0, 0, 0);
    ctrlRowLay->setSpacing(6);

    m_targetGroup = new QGroupBox(tr("Cue Target"));
    m_targetForm  = new QFormLayout(m_targetGroup);
    m_targetForm->setSpacing(3);
    m_targetCombo = new QComboBox;
    m_targetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_targetForm->addRow(tr("Target:"), m_targetCombo);
    ctrlRowLay->addWidget(m_targetGroup);

    m_fadeParamsGroup = new QGroupBox(tr("Parametri Fade"));
    m_fadeParamsForm = new QFormLayout(m_fadeParamsGroup);
    m_fadeParamsForm->setSpacing(3);
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
    m_fadeStopAtEndCheck = new QCheckBox(tr("Stop al termine del fade"));
    m_fadeParamsForm->addRow(tr("Volume target:"), m_fadeTargetVolSpin);
    m_fadeParamsForm->addRow(tr("Durata:"),        m_fadeDurationSpin);
    m_fadeParamsForm->addRow(m_fadeStopAtEndCheck);
    ctrlRowLay->addWidget(m_fadeParamsGroup);

    m_speedGroup = new QGroupBox(tr("Velocità"));
    m_speedForm = new QFormLayout(m_speedGroup);
    m_speedForm->setSpacing(3);
    m_speedRateSpin = new QDoubleSpinBox;
    m_speedRateSpin->setRange(0.1, 10.0);
    m_speedRateSpin->setSingleStep(0.1);
    m_speedRateSpin->setDecimals(2);
    m_speedRateSpin->setSuffix(" ×");
    m_speedRateSpin->setToolTip(tr("< 1 = rallenta, > 1 = velocizza"));
    m_speedForm->addRow(tr("Fattore:"), m_speedRateSpin);
    ctrlRowLay->addWidget(m_speedGroup);
    ctrlRowLay->addStretch();

    ctrlLay->addWidget(ctrlRow);

    // Effect cue: duration + button to open the plugin chain editor
    m_effectGroup = new QGroupBox(tr("Parametri Effetto"));
    m_effectForm = new QFormLayout(m_effectGroup);
    m_effectForm->setSpacing(3);
    m_effectDurSpin = new QDoubleSpinBox;
    m_effectDurSpin->setRange(0.0, 9999.0);
    m_effectDurSpin->setSingleStep(0.1);
    m_effectDurSpin->setDecimals(2);
    m_effectDurSpin->setSuffix(" s");
    m_effectDurSpin->setSpecialValueText(tr("∞ (nessun reset auto)"));
    m_effectDurSpin->setToolTip(tr("0 = effetto permanente fino a Reset Effetti; > 0 = reset automatico dopo N secondi"));
    m_effectFxBtn = new QPushButton(tr("⚙ Catena Effetti..."));
    m_effectFxBtn->setToolTip(tr("Apri la catena di plugin da applicare al target"));
    m_effectForm->addRow(tr("Durata:"), m_effectDurSpin);
    m_effectForm->addRow(m_effectFxBtn);
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

    m_micGroup = new QGroupBox(tr("Ingresso microfono"));
    m_micForm  = new QFormLayout(m_micGroup);
    m_micForm->setSpacing(3);
    m_micDeviceCombo = new QComboBox;
    m_micDeviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_micForm->addRow(tr("Dispositivo:"), m_micDeviceCombo);

    m_micVolumeSpin = new QDoubleSpinBox;
    m_micVolumeSpin->setRange(-60.0, 0.0);
    m_micVolumeSpin->setSingleStep(0.5);
    m_micVolumeSpin->setDecimals(1);
    m_micVolumeSpin->setSuffix(" dB");
    m_micForm->addRow(tr("Volume:"), m_micVolumeSpin);
    micRowLay->addWidget(m_micGroup);
    micRowLay->addStretch();
    micLay->addWidget(micRow);
    propsLay->addWidget(m_micSection);

    // ── Audio-only section: channel routing + waveform ───────
    m_audioSection = new QWidget;
    auto *audioLay = new QVBoxLayout(m_audioSection);
    audioLay->setContentsMargins(0, 2, 0, 0);
    audioLay->setSpacing(4);

    m_recSourceLabel = new QLabel;
    m_recSourceLabel->setStyleSheet("color:#e07020; font-size:10px; padding:1px 3px;");
    m_recSourceLabel->setWordWrap(true);
    m_recSourceLabel->setVisible(false);
    audioLay->addWidget(m_recSourceLabel);


    m_waveformView = new WaveformView;
    m_waveformView->setMinimumHeight(72);

    m_vuL = new VuMeter;
    m_vuR = new VuMeter;
    m_vuL->setToolTip(tr("L"));
    m_vuR->setToolTip(tr("R"));

    auto makeDbLabel = [](QWidget *parent = nullptr) {
        auto *lbl = new QLabel("-∞ dB", parent);
        lbl->setAlignment(Qt::AlignHCenter);
        lbl->setStyleSheet("color:#aaa; font-size:9px;");
        lbl->setFixedWidth(40);
        return lbl;
    };
    m_vuLDbLabel = makeDbLabel();
    m_vuRDbLabel = makeDbLabel();

    auto makeVuCol = [](VuMeter *vu, QLabel *lbl) {
        auto *col = new QWidget;
        auto *lay = new QVBoxLayout(col);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(1);
        lay->addWidget(vu, 1);
        lay->addWidget(lbl);
        return col;
    };

    auto *waveRow = new QWidget;
    auto *waveRowLay = new QHBoxLayout(waveRow);
    waveRowLay->setContentsMargins(0, 0, 0, 0);
    waveRowLay->setSpacing(3);
    waveRowLay->addWidget(m_waveformView, 1);
    waveRowLay->addWidget(makeVuCol(m_vuL, m_vuLDbLabel));
    waveRowLay->addWidget(makeVuCol(m_vuR, m_vuRDbLabel));
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

    // ── Slice table (audio only, shown when slices exist) ──────
    m_sliceSection = new QWidget;
    auto *sliceLay = new QVBoxLayout(m_sliceSection);
    sliceLay->setContentsMargins(0, 2, 0, 0);
    sliceLay->setSpacing(0);

    // Slice table: Seg # | Inizio | Loop | Del
    m_sliceTable = new QTableWidget(0, 4);
    m_sliceTable->setHorizontalHeaderLabels({tr("Seg"), tr("Inizio"), tr("Loop"), ""});
    m_sliceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sliceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_sliceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sliceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_sliceTable->verticalHeader()->setVisible(false);
    m_sliceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sliceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sliceTable->verticalHeader()->setDefaultSectionSize(22);
    m_sliceTable->horizontalHeader()->setFixedHeight(20);
    m_sliceTable->setMaximumHeight(120);
    m_sliceSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_sliceTable->setStyleSheet(
        "QTableWidget { font-size:11px; background:#141826; color:#e2e8f0; }"
        "QTableWidget::item { padding:1px 4px; color:#e2e8f0; background:#141826; }"
        "QHeaderView::section { background:#0f1117; color:#8892a4; border:none;"
        "  border-bottom:1px solid #2a3050; padding:2px 4px; font-size:10px; }"
        "QSpinBox { background:#1e2334; color:#e2e8f0; border:1px solid #2a3050;"
        "  border-radius:2px; padding:1px 2px; font-size:11px; }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  width:14px; background:#252d40; border-left:1px solid #1c2040; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background:#3a4060; }"
        "QSpinBox::up-arrow { image:url(:/icons/arrow-up.svg); width:7px; height:5px; }"
        "QSpinBox::down-arrow { image:url(:/icons/arrow-down.svg); width:7px; height:5px; }"
    );
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

    m_textContentGroup = new QGroupBox(tr("Testo"));
    auto *textContentForm = new QFormLayout(m_textContentGroup);
    textContentForm->setSpacing(3);
    m_textContent = new QTextEdit;
    m_textContent->setMaximumHeight(70);
    m_textContent->setPlaceholderText(tr("Inserisci il testo..."));
    textContentForm->addRow(m_textContent);
    textRowLay->addWidget(m_textContentGroup, 2);

    m_textFmtGroup = new QGroupBox(tr("Formattazione"));
    m_textFmtForm = new QFormLayout(m_textFmtGroup);
    m_textFmtForm->setSpacing(3);

    m_fontFamilyCombo = new QFontComboBox;
    m_fontFamilyCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_textFmtForm->addRow(tr("Font:"), m_fontFamilyCombo);

    m_fontSizeSpin = new QSpinBox;
    m_fontSizeSpin->setRange(6, 300);
    m_fontSizeSpin->setValue(48);
    m_fontSizeSpin->setSuffix(" pt");
    m_fontSizeSpin->setFixedWidth(80);
    m_textFmtForm->addRow(tr("Dimensione:"), m_fontSizeSpin);

    auto *styleRow = new QWidget;
    auto *styleRowLay = new QHBoxLayout(styleRow);
    styleRowLay->setContentsMargins(0, 0, 0, 0);
    styleRowLay->setSpacing(4);
    m_textBoldCheck   = new QCheckBox(tr("Grassetto"));
    m_textItalicCheck = new QCheckBox(tr("Corsivo"));
    styleRowLay->addWidget(m_textBoldCheck);
    styleRowLay->addWidget(m_textItalicCheck);
    styleRowLay->addStretch();
    m_textFmtForm->addRow(styleRow);

    m_textColorBtn = new QPushButton(tr("Colore testo"));
    m_textColorBtn->setFixedHeight(24);
    m_textFmtForm->addRow(m_textColorBtn);

    m_textBgColorBtn = new QPushButton(tr("Colore sfondo"));
    m_textBgColorBtn->setFixedHeight(24);
    m_textFmtForm->addRow(m_textBgColorBtn);

    m_textAlignCombo = new QComboBox;
    m_textAlignCombo->addItem(tr("Sinistra"), int(Qt::AlignLeft  | Qt::AlignVCenter));
    m_textAlignCombo->addItem(tr("Centro"),   int(Qt::AlignCenter));
    m_textAlignCombo->addItem(tr("Destra"),   int(Qt::AlignRight | Qt::AlignVCenter));
    m_textFmtForm->addRow(tr("Allineamento:"), m_textAlignCombo);

    textRowLay->addWidget(m_textFmtGroup, 3);
    textLay->addWidget(textRow);
    propsLay->addWidget(m_textSection);

    // ── Script section ───────────────────────────────────────
    m_scriptSection = new QWidget;
    auto *scriptLay = new QVBoxLayout(m_scriptSection);
    scriptLay->setContentsMargins(0, 4, 0, 0);

    m_scriptRunBtn = new QPushButton(tr("✎  Modifica Script…"));
    scriptLay->addWidget(m_scriptRunBtn);

    propsLay->addWidget(m_scriptSection);

    // ── Record cue section ───────────────────────────────────
    m_recordSection = new QWidget;
    auto *recLay = new QVBoxLayout(m_recordSection);
    recLay->setContentsMargins(0, 4, 0, 0);
    recLay->setSpacing(4);

    m_recGroup = new QGroupBox(tr("Registrazione"));
    m_recForm  = new QFormLayout(m_recGroup);
    m_recForm->setSpacing(3);

    m_recDeviceCombo = new QComboBox;
    m_recDeviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_recForm->addRow(tr("Ingresso:"), m_recDeviceCombo);

    m_recTargetCombo = new QComboBox;
    m_recTargetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_recTargetCombo->setToolTip(tr("Cue audio che riceverà il file registrato dopo lo stop"));
    m_recForm->addRow(tr("Cue audio:"), m_recTargetCombo);

    m_recPathLabel = new QLabel("—");
    m_recPathLabel->setStyleSheet("color:#888; font-size:10px;");
    m_recPathLabel->setWordWrap(true);
    m_recForm->addRow(tr("Ultima rec.:"), m_recPathLabel);

    // Input level meter
    m_recVuMono  = new VuMeter;
    m_recVuMono->setToolTip(tr("Livello ingresso"));
    m_recVuDbLabel = new QLabel("-∞ dB");
    m_recVuDbLabel->setAlignment(Qt::AlignHCenter);
    m_recVuDbLabel->setStyleSheet("color:#aaa; font-size:9px;");
    m_recVuDbLabel->setFixedWidth(40);
    auto *recVuCol = new QWidget;
    auto *recVuColLay = new QVBoxLayout(recVuCol);
    recVuColLay->setContentsMargins(0, 0, 0, 0);
    recVuColLay->setSpacing(1);
    recVuColLay->addWidget(m_recVuMono, 1);
    recVuColLay->addWidget(m_recVuDbLabel);
    auto *recLevelRow = new QWidget;
    auto *recLevelLay = new QHBoxLayout(recLevelRow);
    recLevelLay->setContentsMargins(0, 0, 0, 0);
    recLevelLay->setSpacing(4);
    recLevelLay->addWidget(recVuCol);
    m_recLevelLabel = new QLabel(tr("Livello ingresso"));
    m_recLevelLabel->setStyleSheet("color:#888; font-size:10px;");
    m_recLevelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    recLevelLay->addWidget(m_recLevelLabel, 1);
    recLay->addWidget(m_recGroup);
    recLay->addWidget(recLevelRow);
    propsLay->addWidget(m_recordSection);

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
        const auto fmtDb = [](float db) {
            return db <= -59.5f ? QString("-∞ dB") : QString::number(int(db)) + " dB";
        };
        m_vuLDbLabel->setText(fmtDb(m_vuL->currentDb()));
        m_vuRDbLabel->setText(fmtDb(m_vuR->currentDb()));
    });
    m_vuTimer->start();

    m_recVuTimer = new QTimer(this);
    m_recVuTimer->setInterval(50);
    connect(m_recVuTimer, &QTimer::timeout, this, [this]() {
        auto *rc = qobject_cast<RecordCue*>(m_cue);
        if (!rc) { m_recVuTimer->stop(); return; }
        m_recVuMono->setLevel(rc->inputLevel());
        const float d = m_recVuMono->currentDb();
        m_recVuDbLabel->setText(d <= -59.5f ? "-∞ dB" : QString::number(int(d)) + " dB");
    });

    // ── Stack (normal mode) ──────────────────────────────────
    auto *propsScroll = new QScrollArea;
    propsScroll->setWidget(propsWidget);
    propsScroll->setWidgetResizable(true);
    propsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propsScroll->setFrameShape(QFrame::NoFrame);
    propsScroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; }");

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_emptyWidget);  // index 0
    m_stack->addWidget(propsScroll);    // index 1
    outer->addWidget(m_stack);

    if (const auto *scr = QGuiApplication::primaryScreen())
        setMaximumHeight(scr->availableGeometry().height() / 3);

    // ── Show mode: scrollable stack of compact waveforms ─────
    m_showModeArea = new QScrollArea;
    m_showModeArea->setWidgetResizable(true);
    m_showModeArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_showModeArea->setFrameShape(QFrame::NoFrame);
    m_showModeArea->hide();
    auto *smContent = new QWidget;
    m_showModeContentLay = new QVBoxLayout(smContent);
    m_showModeContentLay->setContentsMargins(4, 4, 4, 4);
    m_showModeContentLay->setSpacing(8);
    m_showModeContentLay->addStretch();
    m_showModeArea->setWidget(smContent);
    outer->addWidget(m_showModeArea);

    m_showPlayTimer = new QTimer(this);
    m_showPlayTimer->setInterval(80);
    connect(m_showPlayTimer, &QTimer::timeout, this, [this] {
        for (const auto &r : std::as_const(m_showRows))
            if (r.cue) r.wv->setPlayPosition(r.cue->position());
    });

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

    // Pitch preserve checkbox
    connect(m_pitchCheck, &QCheckBox::toggled, this, [this](bool v) {
        auto *a = qobject_cast<AudioCue*>(m_cue);
        if (a && !m_loadingFromCue) a->setPitchPreserve(v);
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

    connect(m_recDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        auto *rc = qobject_cast<RecordCue*>(m_cue);
        if (rc && !m_loadingFromCue)
            rc->setInputDeviceId(m_recDeviceCombo->currentData().toString());
    });
    connect(m_recTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        auto *rc = qobject_cast<RecordCue*>(m_cue);
        if (rc && !m_loadingFromCue)
            rc->setLinkedAudioCueId(m_recTargetCombo->currentData().toString());
    });

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

void InspectorPanel::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

QSize InspectorPanel::sizeHint() const {
    const auto *scr = QGuiApplication::primaryScreen();
    const int cap = scr ? scr->availableGeometry().height() / 3 : 280;
    return QSize(QWidget::sizeHint().width(), cap);
}

void InspectorPanel::retranslateUi() {
    // Helper: get QLabel at a given row of a QFormLayout
    auto fl = [](QFormLayout *f, int row) -> QLabel* {
        if (!f) return nullptr;
        auto *item = f->itemAt(row, QFormLayout::LabelRole);
        return item ? qobject_cast<QLabel*>(item->widget()) : nullptr;
    };

    // Empty state label
    if (m_emptyLabel)
        m_emptyLabel->setText(tr("Seleziona una cue per vedere le proprietà"));

    // Play/Stop buttons
    if (m_playBtn) {
        m_playBtn->setToolTip(tr("Play / Pausa / Riprendi"));
        // Preserve "▶  Play" vs "⏸  Pausa" — only update if idle label
        if (!m_playBtn->text().startsWith("⏸"))
            m_playBtn->setText("▶  " + tr("Play"));
    }
    if (m_stopBtn)
        m_stopBtn->setToolTip(tr("Stop"));

    // Group box titles
    if (m_genGroup)         m_genGroup->setTitle(tr("Generale"));
    if (m_timeGroup)        m_timeGroup->setTitle(tr("Timing"));
    if (m_mediaGroup)       m_mediaGroup->setTitle(tr("File sorgente"));
    if (m_fadeGroup)        m_fadeGroup->setTitle(tr("Fade"));
    if (m_targetGroup)      m_targetGroup->setTitle(tr("Cue Target"));
    if (m_fadeParamsGroup)  m_fadeParamsGroup->setTitle(tr("Parametri Fade"));
    if (m_speedGroup)       m_speedGroup->setTitle(tr("Velocità"));
    if (m_effectGroup)      m_effectGroup->setTitle(tr("Parametri Effetto"));
    if (m_micGroup)         m_micGroup->setTitle(tr("Ingresso microfono"));
    if (m_textContentGroup) m_textContentGroup->setTitle(tr("Testo"));
    if (m_textFmtGroup)     m_textFmtGroup->setTitle(tr("Formattazione"));
    if (m_recGroup)         m_recGroup->setTitle(tr("Registrazione"));

    // Generale form
    if (auto *l = fl(m_genForm, 0)) l->setText(tr("Numero:"));
    if (auto *l = fl(m_genForm, 1)) l->setText(tr("Nome:"));
    if (auto *l = fl(m_genForm, 2)) l->setText(tr("Note:"));
    if (m_notesEdit)  m_notesEdit->setPlaceholderText(tr("Note..."));

    // Timing form
    if (auto *l = fl(m_timeForm, 0)) l->setText(tr("Pre-wait:"));
    if (auto *l = fl(m_timeForm, 1)) l->setText(tr("Post-wait:"));
    if (m_autoContinueCheck) m_autoContinueCheck->setText(tr("Auto-continue"));
    if (m_autoFollowCheck)   m_autoFollowCheck->setText(tr("Auto-follow"));

    // Media form
    if (auto *l = fl(m_mediaForm, 0)) l->setText(tr("File:"));
    if (auto *l = fl(m_mediaForm, 1)) l->setText(tr("Volume:"));
    if (auto *l = fl(m_mediaForm, 2)) l->setText(tr("Loop:"));
    if (m_fileEdit)  m_fileEdit->setPlaceholderText(tr("Nessun file..."));
    if (m_loopSpin)  m_loopSpin->setToolTip(tr("Ripetizioni: 0 = loop infinito, 1 = una volta"));

    // Fade form
    if (auto *l = fl(m_fadeForm, 0)) l->setText(tr("In:"));
    if (auto *l = fl(m_fadeForm, 1)) l->setText(tr("Out:"));

    // Channel combo + label
    if (m_uscitaLabel) m_uscitaLabel->setText(tr("Out:"));
    if (m_channelCombo) {
        const int cur = m_channelCombo->currentIndex();
        m_channelCombo->setItemText(0, tr("L + R (stereo)"));
        m_channelCombo->setItemText(1, tr("Solo L"));
        m_channelCombo->setItemText(2, tr("Solo R"));
        m_channelCombo->setCurrentIndex(cur);
    }

    // FX button
    if (m_fxBtn) {
        m_fxBtn->setText(tr("⚙ Effetti..."));
        m_fxBtn->setToolTip(tr("Apri catena effetti per questa cue"));
    }

    // Slice / Rate section
    if (m_rateLabel)   m_rateLabel->setText(tr("Rate:"));
    if (m_rateSpin)    m_rateSpin->setToolTip(tr("Velocità di riproduzione (1.0 = normale)"));
    if (m_pitchCheck) {
        m_pitchCheck->setText(tr("Mantieni pitch"));
#ifdef HAVE_SOUNDTOUCH
        m_pitchCheck->setToolTip(tr("Time-stretch: mantieni la tonalità originale variando il rate"));
#else
        m_pitchCheck->setToolTip(tr("Richiede la libreria soundtouch (non disponibile)"));
#endif
    }
    if (m_slicesLabel)    m_slicesLabel->setText(tr("Slices:"));
    if (m_addSliceBtn)    m_addSliceBtn->setText(tr("+ Aggiungi"));
    if (m_addSliceBtn)    m_addSliceBtn->setToolTip(tr("Aggiunge una slice alla posizione corrente (Ctrl+Click sulla waveform per aggiungere)"));
    if (m_clearSlicesBtn) m_clearSlicesBtn->setText(tr("Rimuovi tutte"));
    if (m_sliceTable)
        m_sliceTable->setHorizontalHeaderLabels({tr("Seg"), tr("Inizio"), tr("Loop"), ""});

    // Control section — target form
    if (auto *l = fl(m_targetForm, 0)) l->setText(tr("Target:"));

    // Fade params form
    if (auto *l = fl(m_fadeParamsForm, 0)) l->setText(tr("Volume target:"));
    if (auto *l = fl(m_fadeParamsForm, 1)) l->setText(tr("Durata:"));
    if (m_fadeStopAtEndCheck) m_fadeStopAtEndCheck->setText(tr("Stop al termine del fade"));

    // Speed form
    if (auto *l = fl(m_speedForm, 0)) l->setText(tr("Fattore:"));
    if (m_speedRateSpin) m_speedRateSpin->setToolTip(tr("< 1 = rallenta, > 1 = velocizza"));

    // Effect form
    if (auto *l = fl(m_effectForm, 0)) l->setText(tr("Durata:"));
    if (m_effectDurSpin) {
        m_effectDurSpin->setSpecialValueText(tr("∞ (nessun reset auto)"));
        m_effectDurSpin->setToolTip(tr("0 = effetto permanente fino a Reset Effetti; > 0 = reset automatico dopo N secondi"));
    }
    if (m_effectFxBtn) {
        m_effectFxBtn->setText(tr("⚙ Catena Effetti..."));
        m_effectFxBtn->setToolTip(tr("Apri la catena di plugin da applicare al target"));
    }

    // Mic form
    if (auto *l = fl(m_micForm, 0)) l->setText(tr("Dispositivo:"));
    if (auto *l = fl(m_micForm, 1)) l->setText(tr("Volume:"));

    // Text section
    if (m_textContent) m_textContent->setPlaceholderText(tr("Inserisci il testo..."));
    if (auto *l = fl(m_textFmtForm, 0)) l->setText(tr("Font:"));
    if (auto *l = fl(m_textFmtForm, 1)) l->setText(tr("Dimensione:"));
    if (auto *l = fl(m_textFmtForm, 5)) l->setText(tr("Allineamento:"));
    if (m_textBoldCheck)   m_textBoldCheck->setText(tr("Grassetto"));
    if (m_textItalicCheck) m_textItalicCheck->setText(tr("Corsivo"));
    if (m_textColorBtn)    m_textColorBtn->setText(tr("Colore testo"));
    if (m_textBgColorBtn)  m_textBgColorBtn->setText(tr("Colore sfondo"));
    if (m_textAlignCombo) {
        const int cur = m_textAlignCombo->currentIndex();
        m_textAlignCombo->setItemText(0, tr("Sinistra"));
        m_textAlignCombo->setItemText(1, tr("Centro"));
        m_textAlignCombo->setItemText(2, tr("Destra"));
        m_textAlignCombo->setCurrentIndex(cur);
    }

    // Record section
    if (auto *l = fl(m_recForm, 0)) l->setText(tr("Ingresso:"));
    if (auto *l = fl(m_recForm, 1)) l->setText(tr("Cue audio:"));
    if (auto *l = fl(m_recForm, 2)) l->setText(tr("Ultima rec.:"));
    if (m_recTargetCombo)  m_recTargetCombo->setToolTip(tr("Cue audio che riceverà il file registrato dopo lo stop"));
    if (m_recLevelLabel)   m_recLevelLabel->setText(tr("Livello ingresso"));

    // Script button
    if (m_scriptRunBtn) m_scriptRunBtn->setText(tr("✎  Modifica Script…"));
}

void InspectorPanel::setCue(Cue *cue) {
    if (m_showMode) return;  // show mode uses addShowAudioCue/removeShowAudioCue

    if (m_cue) {
        disconnect(m_cue, nullptr, this, nullptr);
        m_playTimer->stop();
        m_recVuTimer->stop();
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
    if (auto *rc = qobject_cast<RecordCue*>(m_cue)) {
        connect(rc, &RecordCue::recordingFinished, this, [this](const QString &path) {
            m_recPathLabel->setText(QFileInfo(path).fileName());
        });
    }
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
    const bool isMic       = (t == Cue::Type::Mic);
    const bool isTextCue   = (t == Cue::Type::Text);
    const bool isScriptCue = (t == Cue::Type::Script);
    const bool isRecord    = (t == Cue::Type::Record);

    m_mediaGroup->setVisible(isAudio || isVideo);
    m_fadeGroup->setVisible(isAudio);
    m_audioSection->setVisible(isAudio);
    m_controlSection->setVisible(isControl);
    m_micSection->setVisible(isMic);
    m_textSection->setVisible(isTextCue);
    m_scriptSection->setVisible(isScriptCue);
    m_recordSection->setVisible(isRecord);
    m_uscitaLabel->setVisible(isAudio);
    m_channelCombo->setVisible(isAudio);
    if (m_audioExtrasPanel) m_audioExtrasPanel->setVisible(isAudio);

    if (isAudio) {
        auto *a = static_cast<AudioCue*>(m_cue);

        // Show source RecordCue if one targets this AudioCue
        QString recSrcText;
        for (int i = 0; i < m_cueList->count(); ++i) {
            if (auto *rc = qobject_cast<RecordCue*>(m_cueList->cueAt(i))) {
                if (rc->linkedAudioCueId() == a->id()) {
                    recSrcText = QString("Da reg.: %1 — %2").arg(rc->number(), rc->name());
                    break;
                }
            }
        }
        m_recSourceLabel->setText(recSrcText);
        m_recSourceLabel->setVisible(!recSrcText.isEmpty());

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
        m_pitchCheck->blockSignals(true);
        m_pitchCheck->setChecked(a->pitchPreserve());
        m_pitchCheck->blockSignals(false);
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
    } else if (isRecord) {
        auto *rc = static_cast<RecordCue*>(m_cue);

        // Input device combo
        m_recDeviceCombo->blockSignals(true);
        m_recDeviceCombo->clear();
        m_recDeviceCombo->addItem("Default", QString{});
        for (const auto &dev : QMediaDevices::audioInputs())
            m_recDeviceCombo->addItem(dev.description(), dev.id());
        const int devIdx = m_recDeviceCombo->findData(rc->inputDeviceId());
        m_recDeviceCombo->setCurrentIndex(devIdx >= 0 ? devIdx : 0);
        m_recDeviceCombo->blockSignals(false);

        // Linked audio cue combo — list all AudioCues in the list
        m_recTargetCombo->blockSignals(true);
        m_recTargetCombo->clear();
        m_recTargetCombo->addItem("(nessuna)", QString{});
        for (int i = 0; i < m_cueList->count(); ++i) {
            Cue *c = m_cueList->cueAt(i);
            if (c->type() == Cue::Type::Audio)
                m_recTargetCombo->addItem(
                    QString("%1 — %2").arg(c->number(), c->name()), c->id());
        }
        const int tgtIdx = m_recTargetCombo->findData(rc->linkedAudioCueId());
        m_recTargetCombo->setCurrentIndex(tgtIdx >= 0 ? tgtIdx : 0);
        m_recTargetCombo->blockSignals(false);

        const QString path = rc->lastRecordingPath();
        m_recPathLabel->setText(path.isEmpty() ? "—" : QFileInfo(path).fileName());

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
        if (m_cue && m_cue->type() == Cue::Type::Record)
            m_recVuTimer->start();
        break;
    case Cue::State::Paused:
        m_playTimer->stop();
        m_recVuTimer->stop();
        break;
    default:
        m_playTimer->stop();
        m_recVuTimer->stop();
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
        spin->setToolTip(tr("0 = salta, 1 = riproduci una volta, N = ripeti N volte"));
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

        // Delete button — only for divider rows (i > 0); row 0 is the implicit start
        if (i > 0) {
            auto *delBtn = new QPushButton("×");
            delBtn->setFixedSize(20, 20);
            delBtn->setToolTip(tr("Elimina questa slice"));
            delBtn->setStyleSheet("color:#c44; font-weight:bold; padding:0; border:none;");
            connect(delBtn, &QPushButton::clicked, this, [this, row]() {
                auto *a2 = qobject_cast<AudioCue*>(m_cue);
                if (!a2) return;
                auto sl = a2->slices();
                if (row >= sl.size()) return;
                sl.remove(row);
                // If only segment 0 remains, clear entirely
                a2->setSlices(sl.size() <= 1 ? QVector<AudioSlice>{} : sl);
                const QVector<double> markers = buildSliceMarkers(a2);
                m_waveformView->setSliceMarkers(markers);
                rebuildSliceTable(a2);
            });
            m_sliceTable->setCellWidget(i, 3, delBtn);
        }
    }
    // Fit height exactly to content — no empty scrollable area below last row.
    // Use hardcoded row/header sizes (header()->height() returns 0 before first show).
    const int targetH = 20 + 22 * slices.size() + 2;
    m_sliceTable->setFixedHeight(qMin(targetH, 114));

    m_sliceTable->blockSignals(false);
    Q_UNUSED(dur);
}

// ── Show Mode ─────────────────────────────────────────────────────────────────

void InspectorPanel::setShowMode(bool showMode) {
    m_showMode = showMode;

    if (showMode) {
        // Clear any existing rows from a previous show session
        for (auto &r : m_showRows) delete r.widget;
        m_showRows.clear();
        m_showPlayTimer->stop();

        m_stack->hide();
        m_showModeArea->show();

        // Add already-playing audio cues
        if (m_cueList) {
            for (int i = 0; i < m_cueList->count(); ++i) {
                auto *c = m_cueList->cueAt(i);
                if (c && c->type() == Cue::Type::Audio && c->state() == Cue::State::Playing)
                    addShowAudioCue(static_cast<AudioCue*>(c));
            }
        }
        return;
    }

    // Leaving show mode: clear rows, restore normal view
    m_showPlayTimer->stop();
    for (auto &r : m_showRows) delete r.widget;
    m_showRows.clear();
    m_showModeArea->hide();
    m_stack->show();

    if (m_cue) updateMediaSection();
    const bool edit = true;

    // General / timing / common fields
    m_numberEdit->setEnabled(edit);
    m_nameEdit->setEnabled(edit);
    m_notesEdit->setEnabled(edit);
    m_preWaitSpin->setEnabled(edit);
    m_postWaitSpin->setEnabled(edit);
    m_autoContinueCheck->setEnabled(edit);
    m_autoFollowCheck->setEnabled(edit);

    // Media
    m_browseBtn->setEnabled(edit);
    m_volumeSpin->setEnabled(edit);
    if (m_loopSpin)  m_loopSpin->setEnabled(edit);

    // Fade
    m_fadeInCheck->setEnabled(edit);
    m_fadeInSpin->setEnabled(edit);
    m_fadeOutCheck->setEnabled(edit);
    m_fadeOutSpin->setEnabled(edit);
    m_channelCombo->setEnabled(edit);

    // Slice controls
    if (m_addSliceBtn)    m_addSliceBtn->setEnabled(edit);
    if (m_clearSlicesBtn) m_clearSlicesBtn->setEnabled(edit);
    if (m_rateSpin)       m_rateSpin->setEnabled(edit);
    if (m_sliceTable)     m_sliceTable->setEnabled(edit);

    // Control cue
    m_targetCombo->setEnabled(edit);
    m_fadeTargetVolSpin->setEnabled(edit);
    m_fadeDurationSpin->setEnabled(edit);
    if (m_fadeStopAtEndCheck) m_fadeStopAtEndCheck->setEnabled(edit);
    if (m_speedRateSpin)      m_speedRateSpin->setEnabled(edit);
    if (m_effectDurSpin)      m_effectDurSpin->setEnabled(edit);
    if (m_effectFxBtn)        m_effectFxBtn->setEnabled(edit);

    // Mic
    if (m_micDeviceCombo) m_micDeviceCombo->setEnabled(edit);
    if (m_micVolumeSpin)  m_micVolumeSpin->setEnabled(edit);

    // Text
    if (m_textContent)    m_textContent->setEnabled(edit);
    if (m_fontFamilyCombo)m_fontFamilyCombo->setEnabled(edit);
    if (m_fontSizeSpin)   m_fontSizeSpin->setEnabled(edit);
    if (m_textBoldCheck)  m_textBoldCheck->setEnabled(edit);
    if (m_textItalicCheck)m_textItalicCheck->setEnabled(edit);
    if (m_textColorBtn)   m_textColorBtn->setEnabled(edit);
    if (m_textBgColorBtn) m_textBgColorBtn->setEnabled(edit);
    if (m_textAlignCombo) m_textAlignCombo->setEnabled(edit);

    // Script / Record
    if (m_scriptRunBtn)   m_scriptRunBtn->setEnabled(edit);
    if (m_fxBtn)          m_fxBtn->setEnabled(edit);
    if (m_recDeviceCombo) m_recDeviceCombo->setEnabled(edit);
    if (m_recTargetCombo) m_recTargetCombo->setEnabled(edit);

    // Waveform: always visible, interactive only in edit mode
    m_waveformView->setInteractive(edit);
}

void InspectorPanel::addShowAudioCue(AudioCue *cue) {
    for (const auto &r : std::as_const(m_showRows))
        if (r.cue == cue) return;

    auto *row = new QWidget;
    row->setObjectName("showRow");
    row->setStyleSheet("QWidget#showRow { border-bottom: 1px solid #2a2a2a; padding-bottom: 2px; }");
    auto *lay = new QVBoxLayout(row);
    lay->setContentsMargins(2, 2, 2, 4);
    lay->setSpacing(3);

    const QString lbl = cue->name().isEmpty() ? cue->number() : (cue->number() + " – " + cue->name());
    auto *label = new QLabel(lbl);
    label->setStyleSheet("color:#bbb; font-size:10px; font-weight:bold;");

    auto *wv = new WaveformView;
    wv->setFixedHeight(90);
    wv->setInteractive(false);
    wv->setFilePath(cue->filePath());
    wv->setDuration(cue->duration());
    wv->setFadeIn(cue->fadeInDuration());
    wv->setFadeOut(cue->fadeOutDuration());
    wv->setVolumePoints(cue->volumePoints());
    wv->setTrimStart(cue->trimStart());
    wv->setTrimEnd(cue->trimEnd());
    wv->setSliceMarkers(buildSliceMarkers(cue));

    lay->addWidget(label);
    lay->addWidget(wv);

    // Insert before the trailing stretch
    m_showModeContentLay->insertWidget(m_showModeContentLay->count() - 1, row);
    m_showRows.append({cue, row, wv});

    if (!m_showPlayTimer->isActive())
        m_showPlayTimer->start();
}

void InspectorPanel::removeShowAudioCue(AudioCue *cue) {
    for (int i = 0; i < m_showRows.size(); ++i) {
        if (m_showRows[i].cue == cue) {
            delete m_showRows[i].widget;
            m_showRows.remove(i);
            break;
        }
    }
    if (m_showRows.isEmpty())
        m_showPlayTimer->stop();
}
