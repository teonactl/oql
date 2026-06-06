#include "InspectorPanel.h"
#include "WaveformView.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"
#include "engine/MicCue.h"
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
#include <QFileDialog>
#include <QFileInfo>

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

    m_volumeSpin = makeSpin(1.0);
    m_volumeSpin->setRange(0.0, 1.0);
    m_volumeSpin->setSingleStep(0.05);
    m_volumeSpin->setSuffix({});

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
    m_fadeTargetVolSpin->setRange(0.0, 1.0);
    m_fadeTargetVolSpin->setSingleStep(0.05);
    m_fadeTargetVolSpin->setDecimals(2);
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
    m_micVolumeSpin->setRange(0.0, 1.0);
    m_micVolumeSpin->setSingleStep(0.05);
    m_micVolumeSpin->setDecimals(2);
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
    audioLay->addWidget(m_waveformView, 1);

    propsLay->addWidget(m_audioSection, 1);

    // ── Playhead refresh timer ───────────────────────────────
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(80);
    connect(m_playTimer, &QTimer::timeout, this, &InspectorPanel::refreshPlayhead);

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
    const bool isControl = (t == Cue::Type::Stop || t == Cue::Type::Fade
                         || t == Cue::Type::Pause || t == Cue::Type::Speed
                         || t == Cue::Type::Play);
    const bool isSpeed   = (t == Cue::Type::Speed);
    const bool isMic     = (t == Cue::Type::Mic);

    m_mediaGroup->setVisible(isAudio || isVideo);
    m_fadeGroup->setVisible(isAudio);
    m_audioSection->setVisible(isAudio);
    m_controlSection->setVisible(isControl);
    m_micSection->setVisible(isMic);

    if (isAudio) {
        auto *a = static_cast<AudioCue*>(m_cue);
        m_fileEdit->setText(QFileInfo(a->filePath()).fileName());
        m_volumeSpin->setValue(a->volume());
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
    } else if (isVideo) {
        auto *v = static_cast<VideoCue*>(m_cue);
        m_fileEdit->setText(QFileInfo(v->filePath()).fileName());
        m_volumeSpin->setValue(v->volume());
        m_loopSpin->setValue(v->loopCount());
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
        m_micVolumeSpin->setValue(mc->volume());
        m_micVolumeSpin->blockSignals(false);
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
            m_fadeTargetVolSpin->setValue(fc->targetVolume());
            m_fadeDurationSpin->setValue(fc->fadeDuration());
            m_fadeStopAtEndCheck->setChecked(fc->stopAtEnd());
            m_fadeTargetVolSpin->blockSignals(false);
            m_fadeDurationSpin->blockSignals(false);
            m_fadeStopAtEndCheck->blockSignals(false);
        }
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
        ? "File audio (*.mp3 *.wav *.flac *.ogg *.aac *.m4a *.opus)"
        : "File video (*.mp4 *.mkv *.avi *.mov *.webm *.m4v)";
    const QString path = QFileDialog::getOpenFileName(this, "Scegli file", {}, filter);
    if (path.isEmpty()) return;
    if (isAudio) static_cast<AudioCue*>(m_cue)->setFilePath(path);
    else         static_cast<VideoCue*>(m_cue)->setFilePath(path);
    updateMediaSection();
    loadFromCue();
}

void InspectorPanel::onFilePathChanged() {}

void InspectorPanel::onVolumeChanged(double v) {
    if (!m_cue) return;
    if (m_cue->type() == Cue::Type::Audio) static_cast<AudioCue*>(m_cue)->setVolume(v);
    if (m_cue->type() == Cue::Type::Video) static_cast<VideoCue*>(m_cue)->setVolume(v);
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

    const QString currentId = m_targetCombo->currentData().toString();
    m_targetCombo->blockSignals(true);
    m_targetCombo->clear();
    m_targetCombo->addItem("(nessuno)", QString{});
    for (int i = 0; i < m_cueList->count(); ++i) {
        Cue *c = m_cueList->cueAt(i);
        if (c == m_cue) continue;
        if (mediaOnly && c->type() != Cue::Type::Audio && c->type() != Cue::Type::Video)
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
     || t == Cue::Type::Play  || t == Cue::Type::Speed)
        static_cast<ControlCue*>(m_cue)->setTargetId(m_targetCombo->currentData().toString());
}

void InspectorPanel::onFadeTargetVolChanged(double v) {
    if (m_cue && m_cue->type() == Cue::Type::Fade)
        static_cast<FadeCue*>(m_cue)->setTargetVolume(v);
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
        static_cast<MicCue*>(m_cue)->setVolume(v);
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
