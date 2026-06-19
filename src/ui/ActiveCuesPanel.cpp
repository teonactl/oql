#include "ActiveCuesPanel.h"
#include "engine/CueList.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QTimer>

ActiveCuesPanel::ActiveCuesPanel(CueList *cueList, QWidget *parent)
    : QWidget(parent), m_cueList(cueList)
{
    setMinimumWidth(210);
    setMaximumWidth(300);
    setStyleSheet("ActiveCuesPanel { background:#1a1a1a; }");

    auto *outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);

    auto *titleLbl = new QLabel("  Active Cues");
    titleLbl->setStyleSheet(
        "background:#111111; color:#777777; font-size:11px; font-weight:bold;"
        " padding:4px 6px; border-bottom:1px solid #2a2a2a;");
    titleLbl->setFixedHeight(24);
    outerLay->addWidget(titleLbl);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background:#1a1a1a; border:none; }");
    outerLay->addWidget(scroll, 1);

    m_container = new QWidget;
    m_container->setStyleSheet("background:#1a1a1a;");
    m_containerLay = new QVBoxLayout(m_container);
    m_containerLay->setContentsMargins(4, 4, 4, 4);
    m_containerLay->setSpacing(4);
    m_containerLay->addStretch();
    scroll->setWidget(m_container);

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &ActiveCuesPanel::updateProgress);

    connect(m_cueList, &CueList::cueStateChanged,
            this, &ActiveCuesPanel::onCueStateChanged);
    connect(m_cueList, &CueList::cueRemoved,
            this, [this](int) { rebuildCards(); });
}

void ActiveCuesPanel::onCueStateChanged(int, Cue::State) {
    rebuildCards();

    bool anyActive = false;
    for (int i = 0; i < m_cueList->count(); ++i) {
        const Cue::State s = m_cueList->cueAt(i)->state();
        if (s == Cue::State::Playing || s == Cue::State::Paused
                || s == Cue::State::Waiting) {
            anyActive = true;
            break;
        }
    }
    if (anyActive) m_timer->start();
    else           m_timer->stop();
}

void ActiveCuesPanel::rebuildCards() {
    // Remove all card widgets (keep the trailing stretch)
    while (m_containerLay->count() > 1) {
        QLayoutItem *item = m_containerLay->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_cards.clear();

    for (int i = 0; i < m_cueList->count(); ++i) {
        Cue *cue = m_cueList->cueAt(i);
        if (cue->state() == Cue::State::Idle) continue;

        const Cue::State st = cue->state();
        const bool waiting = (st == Cue::State::Waiting);
        const bool playing = (st == Cue::State::Playing);

        auto *card = new QFrame;
        const QString chunkColor = waiting ? "#ddaa33" : "#44cc88";
        card->setStyleSheet(
            "QFrame { background:#252525; border:1px solid #333333; border-radius:4px; }"
            "QLabel { background:transparent; color:#cccccc; font-size:11px; }"
            "QProgressBar { background:#1a1a1a; border:none; border-radius:2px; }"
            "QProgressBar::chunk { background:" + chunkColor + "; border-radius:2px; }");

        auto *cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(6, 4, 6, 4);
        cardLay->setSpacing(3);

        // ── Top row ───────────────────────────────────────────
        auto *topRow = new QWidget;
        auto *topLay = new QHBoxLayout(topRow);
        topLay->setContentsMargins(0, 0, 0, 0);
        topLay->setSpacing(4);

        auto *dotLbl = new QLabel(waiting ? "⏳" : "●");
        dotLbl->setStyleSheet(waiting
            ? "color:#ddaa33; font-size:10px;"
            : (playing ? "color:#44dd88; font-size:10px;" : "color:#ddbb44; font-size:10px;"));
        dotLbl->setFixedWidth(14);
        topLay->addWidget(dotLbl);

        const QString num = cue->number().isEmpty()
            ? QString::number(i + 1) : cue->number();
        auto *numLbl = new QLabel("#" + num);
        numLbl->setStyleSheet("color:#ffffff; font-weight:bold; font-size:11px;");
        topLay->addWidget(numLbl);

        QColor badgeColor;
        switch (cue->type()) {
        case Cue::Type::Audio: badgeColor = QColor(0x2a, 0x6d, 0xcc); break;
        case Cue::Type::Video: badgeColor = QColor(0x2a, 0x88, 0x44); break;
        case Cue::Type::Image: badgeColor = QColor(0xb0, 0x3a, 0x6b); break;
        case Cue::Type::Stop:        badgeColor = QColor(0xcc, 0x33, 0x33); break;
        case Cue::Type::Fade:        badgeColor = QColor(0xcc, 0x77, 0x22); break;
        case Cue::Type::Pause:       badgeColor = QColor(0x88, 0x55, 0xcc); break;
        case Cue::Type::Speed:       badgeColor = QColor(0x11, 0x99, 0x99); break;
        case Cue::Type::Play:        badgeColor = QColor(0x22, 0xaa, 0x55); break;
        case Cue::Type::Effect:      badgeColor = QColor(0x7b, 0x35, 0x9e); break;
        case Cue::Type::ResetEffect: badgeColor = QColor(0x35, 0x7b, 0x9e); break;
        default:                     badgeColor = QColor(0x44, 0x48, 0x58); break;
        }
        auto *typeLbl = new QLabel(cue->typeName());
        typeLbl->setStyleSheet(QString(
            "color:#fff; background:%1; border-radius:3px; padding:0px 4px;"
            " font-size:10px; font-weight:bold;").arg(badgeColor.name()));
        topLay->addWidget(typeLbl);

        const QString nameStr = cue->name().isEmpty() ? "(senza nome)" : cue->name();
        auto *nameLbl = new QLabel(nameStr);
        nameLbl->setStyleSheet("color:#cccccc; font-size:11px;");
        nameLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        nameLbl->setMinimumWidth(0);
        nameLbl->setToolTip(nameStr);
        topLay->addWidget(nameLbl, 1);

        auto *stopBtn = new QPushButton("✕");
        stopBtn->setFixedSize(18, 18);
        stopBtn->setStyleSheet(
            "QPushButton { background:#553333; color:#ff6666; border:none;"
            " border-radius:3px; font-size:10px; font-weight:bold; }"
            "QPushButton:hover { background:#884444; }");
        connect(stopBtn, &QPushButton::clicked, cue, &Cue::stop);
        topLay->addWidget(stopBtn);

        cardLay->addWidget(topRow);

        // ── Progress bar ──────────────────────────────────────
        auto *progressBar = new QProgressBar;
        progressBar->setRange(0, 1000);
        progressBar->setTextVisible(false);
        progressBar->setFixedHeight(4);

        double barValue = 0.0;
        if (waiting) {
            const double total = cue->waitTotal();
            barValue = total > 0.0 ? cue->waitElapsed() / total : 0.0;
        } else {
            const double dur = cue->duration();
            barValue = dur > 0.0 ? cue->position() / dur : 0.0;
        }
        progressBar->setValue(int(qBound(0.0, barValue, 1.0) * 1000.0));
        cardLay->addWidget(progressBar);

        // ── Time row ──────────────────────────────────────────
        auto *timeRow = new QWidget;
        auto *timeLay = new QHBoxLayout(timeRow);
        timeLay->setContentsMargins(0, 0, 0, 0);
        timeLay->setSpacing(0);

        auto *elapsedLbl   = new QLabel;
        auto *remainingLbl = new QLabel;
        elapsedLbl->setStyleSheet("color:#777777; font-size:10px; font-family:monospace;");
        remainingLbl->setStyleSheet("color:#777777; font-size:10px; font-family:monospace;");
        remainingLbl->setAlignment(Qt::AlignRight);

        if (waiting) {
            const double elapsed = cue->waitElapsed();
            const double rem     = qMax(0.0, cue->waitTotal() - elapsed);
            elapsedLbl->setText("pre-wait");
            remainingLbl->setText(QString("-%1s").arg(rem, 0, 'f', 1));
        } else {
            const double dur = cue->duration();
            const double pos = cue->position();
            if (dur > 0.0) {
                elapsedLbl->setText(QString("+%1s").arg(pos, 0, 'f', 1));
                remainingLbl->setText(QString("-%1s").arg(qMax(0.0, dur - pos), 0, 'f', 1));
            } else {
                elapsedLbl->setText("—");
            }
        }

        timeLay->addWidget(elapsedLbl);
        timeLay->addStretch();
        timeLay->addWidget(remainingLbl);
        cardLay->addWidget(timeRow);

        m_containerLay->insertWidget(m_containerLay->count() - 1, card);
        m_cards[cue] = { dotLbl, progressBar, elapsedLbl, remainingLbl };
    }
}

void ActiveCuesPanel::updateProgress() {
    bool needRebuild = false;

    for (auto it = m_cards.begin(); it != m_cards.end(); ++it) {
        Cue        *cue = it.key();
        auto       &w   = it.value();
        const auto  st  = cue->state();

        if (st == Cue::State::Waiting) {
            const double total   = cue->waitTotal();
            const double elapsed = cue->waitElapsed();
            const double rem     = qMax(0.0, total - elapsed);
            w.progressBar->setValue(total > 0.0 ? int(qMin(elapsed / total, 1.0) * 1000.0) : 0);
            w.remainingLbl->setText(QString("-%1s").arg(rem, 0, 'f', 1));
        } else {
            const bool   playing = (st == Cue::State::Playing);
            const double dur     = cue->duration();
            const double pos     = cue->position();

            w.progressBar->setValue(dur > 0.0 ? int(pos / dur * 1000.0) : 0);

            if (dur > 0.0) {
                w.elapsedLbl->setText(QString("+%1s").arg(pos, 0, 'f', 1));
                w.remainingLbl->setText(QString("-%1s").arg(qMax(0.0, dur - pos), 0, 'f', 1));
            }

            w.dotLbl->setStyleSheet(playing
                ? "color:#44dd88; font-size:10px;"
                : "color:#ddbb44; font-size:10px;");
        }

        // Rebuild if a cue finished its wait (now playing) or finished playing
        if (st == Cue::State::Idle) needRebuild = true;
    }

    if (needRebuild) rebuildCards();
}
