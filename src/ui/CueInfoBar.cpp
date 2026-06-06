#include "CueInfoBar.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QFileInfo>

static const char *kBarStyle =
    "CueInfoBar { background:#1e1e1e; border-top:1px solid #3a3a3a; }"
    "QLabel { color:#d0d0d0; font-size:12px; background:transparent; }";

static const char *kDotIdle    = "color:#555555; font-size:16px;";
static const char *kDotPlaying = "color:#44dd88; font-size:16px;";
static const char *kDotPaused  = "color:#ddbb44; font-size:16px;";

static const char *kTypeBadgeAudio =
    "color:#fff; background:#2a6dcc; border-radius:3px; padding:1px 5px; font-size:11px; font-weight:bold;";
static const char *kTypeBadgeVideo =
    "color:#fff; background:#2a8844; border-radius:3px; padding:1px 5px; font-size:11px; font-weight:bold;";

CueInfoBar::CueInfoBar(QWidget *parent) : QFrame(parent) {
    setStyleSheet(kBarStyle);
    setFixedHeight(36);
    buildUi();
    setCue(nullptr);
}

void CueInfoBar::buildUi() {
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 0, 10, 0);
    lay->setSpacing(10);

    m_dotLbl    = new QLabel("●");
    m_dotLbl->setFixedWidth(16);
    m_numberLbl = new QLabel;
    m_numberLbl->setFixedWidth(40);
    m_numberLbl->setStyleSheet("font-weight:bold; color:#ffffff;");
    m_typeLbl   = new QLabel;
    m_typeLbl->setFixedWidth(50);
    m_nameLbl   = new QLabel;
    m_nameLbl->setMinimumWidth(100);
    m_fileLbl   = new QLabel;
    m_fileLbl->setStyleSheet("color:#888888;");

    auto *sep1 = new QLabel("|");
    sep1->setStyleSheet("color:#444;");
    auto *sep2 = new QLabel("|");
    sep2->setStyleSheet("color:#444;");
    auto *sep3 = new QLabel("|");
    sep3->setStyleSheet("color:#444;");

    m_timeLbl   = new QLabel;
    m_timeLbl->setStyleSheet("color:#aaaaaa; font-family:monospace;");
    m_timeLbl->setFixedWidth(110);
    m_volLbl    = new QLabel;
    m_volLbl->setStyleSheet("color:#aaaaaa;");
    m_volLbl->setFixedWidth(70);

    m_emptyLbl  = new QLabel("Nessuna cue selezionata");
    m_emptyLbl->setStyleSheet("color:#555; font-style:italic;");

    lay->addWidget(m_emptyLbl);
    lay->addWidget(m_dotLbl);
    lay->addWidget(m_numberLbl);
    lay->addWidget(m_typeLbl);
    lay->addWidget(m_nameLbl, 1);
    lay->addWidget(sep1);
    lay->addWidget(m_fileLbl, 1);
    lay->addWidget(sep2);
    lay->addWidget(m_timeLbl);
    lay->addWidget(sep3);
    lay->addWidget(m_volLbl);

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &CueInfoBar::refresh);
}

void CueInfoBar::setCue(Cue *cue) {
    if (m_cue) {
        disconnect(m_cue, nullptr, this, nullptr);
    }
    m_cue = cue;
    m_timer->stop();

    if (m_cue) {
        connect(m_cue, &Cue::stateChanged,   this, &CueInfoBar::refresh);
        connect(m_cue, &Cue::propertyChanged, this, &CueInfoBar::refresh);
        if (m_cue->state() == Cue::State::Playing)
            m_timer->start();
    }
    updateDisplay();
}

void CueInfoBar::refresh() {
    if (!m_cue) return;
    const bool playing = (m_cue->state() == Cue::State::Playing);
    if (playing && !m_timer->isActive()) m_timer->start();
    if (!playing)                        m_timer->stop();
    updateDisplay();
}

void CueInfoBar::updateDisplay() {
    const bool hasCue = (m_cue != nullptr);
    m_emptyLbl->setVisible(!hasCue);
    m_dotLbl->setVisible(hasCue);
    m_numberLbl->setVisible(hasCue);
    m_typeLbl->setVisible(hasCue);
    m_nameLbl->setVisible(hasCue);
    m_fileLbl->setVisible(hasCue);
    m_timeLbl->setVisible(hasCue);
    m_volLbl->setVisible(hasCue);

    if (!hasCue) return;

    const Cue::State st = m_cue->state();

    // Status dot
    if      (st == Cue::State::Playing) m_dotLbl->setStyleSheet(kDotPlaying);
    else if (st == Cue::State::Paused)  m_dotLbl->setStyleSheet(kDotPaused);
    else                                m_dotLbl->setStyleSheet(kDotIdle);

    // Number
    const QString num = m_cue->number().isEmpty()
                        ? QString("—") : m_cue->number();
    m_numberLbl->setText("#" + num);

    // Type badge
    const bool isAudio = (m_cue->type() == Cue::Type::Audio);
    const bool isVideo = (m_cue->type() == Cue::Type::Video);
    m_typeLbl->setText(m_cue->typeName());
    m_typeLbl->setStyleSheet(isAudio ? kTypeBadgeAudio : (isVideo ? kTypeBadgeVideo : "color:#fff; background:#555; border-radius:3px; padding:1px 5px; font-size:11px; font-weight:bold;"));

    // Name
    const QString name = m_cue->name().isEmpty() ? "(senza nome)" : m_cue->name();
    m_nameLbl->setText(name);

    // File (only for Audio / Video)
    QString filePath;
    if (isAudio)      filePath = static_cast<AudioCue*>(m_cue)->filePath();
    else if (isVideo) filePath = static_cast<VideoCue*>(m_cue)->filePath();
    m_fileLbl->setText(filePath.isEmpty() ? "—" : QFileInfo(filePath).fileName());

    // Time (pos / dur or remaining)
    const double dur = m_cue->duration();
    const double pos = m_cue->position();
    if (dur > 0.0) {
        const double rem = qMax(0.0, dur - pos);
        if (st == Cue::State::Playing) {
            m_timeLbl->setText(QString("-%1 / %2")
                .arg(rem, 0, 'f', 1)
                .arg(dur, 0, 'f', 1));
        } else {
            m_timeLbl->setText(QString("%1 s").arg(dur, 0, 'f', 2));
        }
    } else {
        m_timeLbl->setText("—");
    }

    // Volume (only for Audio / Video)
    double vol = 1.0;
    if (isAudio)      vol = static_cast<AudioCue*>(m_cue)->volume();
    else if (isVideo) vol = static_cast<VideoCue*>(m_cue)->volume();
    m_volLbl->setText((isAudio || isVideo) ? QString("Vol %1%").arg(qRound(vol * 100)) : "—");
}
