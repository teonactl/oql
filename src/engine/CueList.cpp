#include "CueList.h"
#include "AudioCue.h"
#include "VideoCue.h"
#include "ControlCues.h"
#include "MicCue.h"
#include "GroupCue.h"
#include "LabelCue.h"
#include "TextCue.h"
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

CueList::CueList(QObject *parent) : QObject(parent) {}

void CueList::addCue(std::unique_ptr<Cue> cue, int index) {
    const int n = int(m_cues.size());
    if (index < 0 || index > n) index = n;

    connectCue(cue.get());
    m_cues.insert(m_cues.begin() + index, std::move(cue));
    emit cueAdded(index);
}

void CueList::removeCue(int index) {
    if (index < 0 || index >= int(m_cues.size())) return;
    m_cues[index]->stop();
    m_cues.erase(m_cues.begin() + index);
    if (m_playhead > index) m_playhead--;
    m_playhead = qBound(0, m_playhead, int(m_cues.size()));
    emit cueRemoved(index);
}

void CueList::moveCue(int from, int to) {
    const int n = int(m_cues.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    auto cue = std::move(m_cues[from]);
    m_cues.erase(m_cues.begin() + from);
    const int insertAt = to > from ? to : to;
    m_cues.insert(m_cues.begin() + insertAt, std::move(cue));
    emit cueMoved(from, to);
}

Cue* CueList::cueAt(int index) const {
    if (index < 0 || index >= int(m_cues.size())) return nullptr;
    return m_cues[index].get();
}

Cue* CueList::findCueById(const QString &id) const {
    if (id.isEmpty()) return nullptr;
    for (const auto &c : m_cues)
        if (c->id() == id) return c.get();
    return nullptr;
}

void CueList::setPlayhead(int index) {
    index = qBound(0, index, int(m_cues.size()));
    if (m_playhead != index) {
        m_playhead = index;
        emit playheadChanged(m_playhead);
    }
}

void CueList::go() {
    if (m_cues.empty()) return;
    if (m_playhead >= int(m_cues.size()))
        setPlayhead(0);
    Cue *cue = m_cues[m_playhead].get();

    // GroupCue: redirect to first child; skip if empty
    if (cue->type() == Cue::Type::Group) {
        const QString gid = cue->id();
        for (int i = 0; i < int(m_cues.size()); ++i) {
            if (m_cues[i]->parentGroupId() == gid) {
                setPlayhead(i);
                go();
                return;
            }
        }
        // Empty group — advance past it
        setPlayhead(m_playhead + 1);
        return;
    }

    const bool   ac   = cue->autoContinue();
    const double pre  = cue->preWait();
    const double post = cue->postWait();
    setPlayhead(m_playhead + 1);
    if (auto *cc = dynamic_cast<ControlCue*>(cue))
        cc->setTarget(findCueById(cc->targetId()));

    auto doStart = [this, cue, ac, post]() {
        cue->go();
        if (ac) {
            if (post > 0.001)
                QTimer::singleShot(int(post * 1000), this, [this]{ go(); });
            else
                go();
        }
    };

    if (pre > 0.001) {
        cue->beginPreWait(pre);
        QTimer::singleShot(int(pre * 1000), this, [this, cue, doStart]() {
            const bool exists = std::any_of(m_cues.begin(), m_cues.end(),
                                            [cue](const auto &c){ return c.get() == cue; });
            if (!exists || cue->state() != Cue::State::Waiting) return;
            doStart();
        });
    } else {
        doStart();
    }
}

void CueList::stopAll() {
    for (auto &c : m_cues) c->stop();
}

void CueList::pauseAll() {
    for (auto &c : m_cues)
        if (c->state() == Cue::State::Playing) c->pause();
}

void CueList::connectCue(Cue *cue) {
    connect(cue, &Cue::stateChanged, this, [this, cue](Cue::State s) {
        auto it = std::find_if(m_cues.begin(), m_cues.end(),
                               [cue](const auto &c){ return c.get() == cue; });
        if (it == m_cues.end()) return;
        const int idx = int(std::distance(m_cues.begin(), it));
        emit cueStateChanged(idx, s);
    });

    connect(cue, &Cue::propertyChanged, this, [this, cue]() {
        auto it = std::find_if(m_cues.begin(), m_cues.end(),
                               [cue](const auto &c){ return c.get() == cue; });
        if (it == m_cues.end()) return;
        emit cuePropertyChanged(int(std::distance(m_cues.begin(), it)));
    });

    connect(cue, &Cue::finished, this, [this, cue]() {
        if (!cue->autoFollow()) return;
        auto it = std::find_if(m_cues.begin(), m_cues.end(),
                               [cue](const auto &c){ return c.get() == cue; });
        if (it == m_cues.end()) return;
        const int next = int(std::distance(m_cues.begin(), it)) + 1;
        if (next >= int(m_cues.size())) return;

        const double post = cue->postWait();
        // Route through go() so the next cue's preWait and autoContinue/autoFollow are respected
        auto fireNext = [this, next]() {
            if (next >= int(m_cues.size())) return;
            setPlayhead(next);
            go();
        };

        if (post > 0.001)
            QTimer::singleShot(int(post * 1000), this, fireNext);
        else
            fireNext();
    });
}

QJsonArray CueList::toJson() const {
    QJsonArray arr;
    for (const auto &c : m_cues) arr.append(c->toJson());
    return arr;
}

void CueList::fromJson(const QJsonArray &arr) {
    emit aboutToReset();
    for (auto &c : m_cues) c->stop();
    m_cues.clear();
    m_playhead = 0;
    for (const auto &val : arr) {
        const auto obj  = val.toObject();
        const QString t = obj["cueType"].toString();
        std::unique_ptr<Cue> cue;
        if (t == "audio") {
            auto a = std::make_unique<AudioCue>();
            a->fromJson(obj);
            cue = std::move(a);
        } else if (t == "video") {
            auto v = std::make_unique<VideoCue>();
            v->fromJson(obj);
            cue = std::move(v);
        } else if (t == "stop") {
            auto c = std::make_unique<StopCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "fade") {
            auto c = std::make_unique<FadeCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "pause") {
            auto c = std::make_unique<PauseCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "speed") {
            auto c = std::make_unique<SpeedCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "play") {
            auto c = std::make_unique<PlayCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "mic") {
            auto c = std::make_unique<MicCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "group") {
            auto c = std::make_unique<GroupCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "label") {
            auto c = std::make_unique<LabelCue>();
            c->fromJson(obj);
            cue = std::move(c);
        } else if (t == "text") {
            auto c = std::make_unique<TextCue>();
            c->fromJson(obj);
            cue = std::move(c);
        }
        if (cue) {
            connectCue(cue.get());
            m_cues.push_back(std::move(cue));
        }
    }
    emit listReset();
}
