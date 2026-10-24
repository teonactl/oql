#include "CueList.h"
#include "AudioCue.h"
#include "VideoCue.h"
#include "ControlCues.h"
#include "MicCue.h"
#include "GroupCue.h"
#include "LabelCue.h"
#include "TextCue.h"
#include "ImageCue.h"
#include "ScriptCue.h"
#include "RecordCue.h"
#include "ScriptEngine.h"
#include <QJsonObject>
#include <QTimer>
#include <QDebug>
#include <QHash>
#include <algorithm>
#include <functional>

CueList::CueList(QObject *parent) : QObject(parent) {
    ScriptEngine::instance().init(this);
}

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

    // Aggiusta il playhead per la cue rimossa e notifica SEMPRE se cambia:
    // prima si limitava a scrivere m_playhead senza emettere playheadChanged,
    // lasciando la selezione in vista (MainWindow) disallineata da quale cue
    // GO avrebbe effettivamente fatto partire.
    int newPlayhead = m_playhead;
    if (newPlayhead > index) --newPlayhead;
    newPlayhead = qBound(0, newPlayhead, int(m_cues.size()));
    if (newPlayhead != m_playhead) {
        m_playhead = newPlayhead;
        emit playheadChanged(m_playhead);
    }

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

int CueList::nextNonLabel(int from) const {
    while (from < int(m_cues.size()) && m_cues[from]->type() == Cue::Type::Label)
        ++from;
    return from;
}

int CueList::nextInSequence(int from) const {
    if (from < 0 || from >= int(m_cues.size()))
        return int(m_cues.size());

    const QString pgid = m_cues[from]->parentGroupId();

    if (pgid.isEmpty()) {
        // Root cue: simple linear advance
        return nextNonLabel(from + 1);
    }

    // Child cue: scan forward for the next sibling (higher actual index, same group).
    // We do NOT wrap around — children are played in ascending actual-index order.
    for (int i = from + 1; i < int(m_cues.size()); ++i) {
        if (m_cues[i]->parentGroupId() == pgid)
            return m_cues[i]->type() == Cue::Type::Label ? nextNonLabel(i) : i;
    }

    // No next sibling: we're at the last child. Jump past the parent group.
    for (int i = 0; i < int(m_cues.size()); ++i) {
        if (m_cues[i]->id() == pgid) {
            int next = i + 1;
            // Skip any children that immediately follow the group in actual order
            while (next < int(m_cues.size()) && m_cues[next]->parentGroupId() == pgid)
                ++next;
            return nextNonLabel(next);
        }
    }

    return nextNonLabel(from + 1); // unreachable fallback
}

void CueList::go() {
    goAdvance(0);
}

// Implementazione reale di go(). `depth` cresce solo lungo i passi fatti SENZA
// nessuna attesa reale (redirect su gruppo, auto-continue con post-wait nullo):
// quelli sono gli unici punti che potrebbero richiamarsi all'infinito se un
// gruppo si riferisce a se stesso (o due gruppi si parentano a vicenda) o se
// un'intera lista auto-continue wrappa a inizio lista. I passi con un'attesa
// reale (pre/post-wait > 0) resettano `depth` a 0, perché sono già limitati
// dal tempo reale e non sono a rischio di loop stretto.
//
// Ogni auto-richiamo passa dall'event loop (QTimer::singleShot) invece di
// chiamare go()/goAdvance() direttamente: così lo stack non cresce mai con la
// lunghezza della catena, a prescindere dal contatore di profondità.
void CueList::goAdvance(int depth) {
    if (depth > kMaxAutoAdvanceDepth) {
        qWarning("CueList: catena auto-continue/gruppo interrotta dopo %d passi "
                 "senza attese reali (possibile riferimento ciclico tra cue/gruppi)",
                 kMaxAutoAdvanceDepth);
        return;
    }

    if (m_cues.empty()) return;
    if (m_playhead >= int(m_cues.size()))
        setPlayhead(0);

    // Skip labels: if the user manually selected a label and pressed GO, jump past it
    {
        const int ph = nextNonLabel(m_playhead);
        if (ph != m_playhead) setPlayhead(ph);
    }
    if (m_playhead >= int(m_cues.size())) return;

    Cue *cue = m_cues[m_playhead].get();

    // GroupCue: expand (deferred), then redirect to first child; skip if empty
    if (cue->type() == Cue::Type::Group) {
        const QString gid = cue->id();
        // Defer setCollapsed so the model reset doesn't fire inside go()
        if (auto *gc = dynamic_cast<GroupCue*>(cue); gc && gc->collapsed()) {
            QTimer::singleShot(0, this, [this, gid]() {
                if (auto *g = dynamic_cast<GroupCue*>(findCueById(gid)))
                    g->setCollapsed(false);
            });
        }
        for (int i = 0; i < int(m_cues.size()); ++i) {
            if (m_cues[i]->parentGroupId() == gid) {
                setPlayhead(i);
                QTimer::singleShot(0, this, [this, depth]{ goAdvance(depth + 1); });
                return;
            }
        }
        // Empty group — advance past it
        setPlayhead(nextInSequence(m_playhead));
        return;
    }

    const bool   ac   = cue->autoContinue();
    const double pre  = cue->preWait();
    const double post = cue->postWait();
    setPlayhead(nextInSequence(m_playhead));
    if (auto *cc = dynamic_cast<ControlCue*>(cue))
        cc->setTarget(findCueById(cc->targetId()));

    auto doStart = [this, cue, ac, post, depth]() {
        cue->go();
        if (ac) {
            if (post > 0.001)
                QTimer::singleShot(int(post * 1000), this, [this]{ goAdvance(0); });
            else
                QTimer::singleShot(0, this, [this, depth]{ goAdvance(depth + 1); });
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

// R5: vedi commento nell'header.
int CueList::indexOfCue(const Cue *cue) const {
    auto it = std::find_if(m_cues.begin(), m_cues.end(),
                           [cue](const auto &c){ return c.get() == cue; });
    if (it == m_cues.end()) return -1;
    return int(std::distance(m_cues.begin(), it));
}

void CueList::connectCue(Cue *cue) {
    connect(cue, &Cue::stateChanged, this, [this, cue](Cue::State s) {
        const int idx = indexOfCue(cue);
        if (idx < 0) return;
        emit cueStateChanged(idx, s);

        // Auto-collapse group when its last active child goes Idle
        if (s == Cue::State::Idle && !cue->parentGroupId().isEmpty()) {
            const QString gid = cue->parentGroupId();
            const bool anyActive = std::any_of(m_cues.begin(), m_cues.end(),
                [&gid](const auto &c) {
                    return c->parentGroupId() == gid && c->state() != Cue::State::Idle;
                });
            if (!anyActive) {
                // Keep open while the playhead is still inside the group (more children to fire)
                const bool playheadInGroup = m_playhead < int(m_cues.size()) &&
                                             m_cues[m_playhead]->parentGroupId() == gid;
                if (!playheadInGroup) {
                    QTimer::singleShot(0, this, [this, gid]() {
                        int groupIdx = -1;
                        for (int i = 0; i < int(m_cues.size()); ++i) {
                            if (m_cues[i]->id() == gid) { groupIdx = i; break; }
                        }
                        int nextIdx = groupIdx + 1;
                        while (nextIdx < int(m_cues.size()) &&
                               m_cues[nextIdx]->parentGroupId() == gid)
                            ++nextIdx;
                        nextIdx = nextNonLabel(nextIdx);

                        if (auto *gc = dynamic_cast<GroupCue*>(findCueById(gid)))
                            gc->setCollapsed(true);

                        if (nextIdx < int(m_cues.size()))
                            setPlayhead(nextIdx);
                        else if (groupIdx >= 0)
                            setPlayhead(groupIdx);
                    });
                }
            }
        }
    });

    connect(cue, &Cue::propertyChanged, this, [this, cue]() {
        const int idx = indexOfCue(cue);
        if (idx < 0) return;
        emit cuePropertyChanged(idx);
    });

    // displayChanged: UI-only refresh (e.g. duration becoming available) — must NOT mark workspace modified
    connect(cue, &Cue::displayChanged, this, [this, cue]() {
        const int idx = indexOfCue(cue);
        if (idx < 0) return;
        emit cueLayoutChanged(idx);
    });

    // Group collapse/expand: triggers view rebuild but must NOT mark workspace modified
    if (auto *gc = dynamic_cast<GroupCue*>(cue)) {
        connect(gc, &GroupCue::layoutChanged, this, [this, cue]() {
            const int idx = indexOfCue(cue);
            if (idx < 0) return;
            emit cueLayoutChanged(idx);
        });
    }

    connect(cue, &Cue::finished, this, [this, cue]() {
        if (!cue->autoFollow()) return;
        const int actualIdx = indexOfCue(cue);
        if (actualIdx < 0) return;

        const double post = cue->postWait();
        auto fireNext = [this, actualIdx]() {
            const int ph = nextInSequence(actualIdx);
            if (ph >= int(m_cues.size())) return;
            setPlayhead(ph);
            go();
        };

        if (post > 0.001)
            QTimer::singleShot(int(post * 1000), this, fireNext);
        else
            fireNext();
    });

#ifndef OQL_BASE
    if (auto *rc = qobject_cast<RecordCue*>(cue)) {
        connect(rc, &RecordCue::requestSetFilePath, this, [this](const QString &id, const QString &path) {
            if (auto *ac = qobject_cast<AudioCue*>(findCueById(id)))
                ac->setFilePath(path);
        });
    }
#endif
}

QJsonArray CueList::toJson() const {
    QJsonArray arr;
    for (const auto &c : m_cues) arr.append(c->toJson());
    return arr;
}

// R4: tabella statica stringa→factory, sostituisce la precedente catena
// if/else di 16 rami identici (make_unique<T>() + fromJson + move). Il
// fromJson(obj) specifico per tipo resta a carico del chiamante, dopo la
// factory: Cue::fromJson() è virtuale, quindi la dispatch sul tipo concreto
// avviene comunque anche passando per il puntatore alla base.
static const QHash<QString, std::function<std::unique_ptr<Cue>()>> &cueFactories() {
    static const QHash<QString, std::function<std::unique_ptr<Cue>()>> table = {
        {"audio", [] { return std::make_unique<AudioCue>(); }},
        {"video", [] { return std::make_unique<VideoCue>(); }},
        {"stop",  [] { return std::make_unique<StopCue>(); }},
        {"fade",  [] { return std::make_unique<FadeCue>(); }},
        {"pause", [] { return std::make_unique<PauseCue>(); }},
#ifndef OQL_BASE
        {"speed", [] { return std::make_unique<SpeedCue>(); }},
#endif
        {"play",  [] { return std::make_unique<PlayCue>(); }},
        {"mic",   [] { return std::make_unique<MicCue>(); }},
        {"group", [] { return std::make_unique<GroupCue>(); }},
        {"label", [] { return std::make_unique<LabelCue>(); }},
        {"text",  [] { return std::make_unique<TextCue>(); }},
        {"image", [] { return std::make_unique<ImageCue>(); }},
#ifndef OQL_BASE
        {"effect",      [] { return std::make_unique<EffectCue>(); }},
        {"reseteffect", [] { return std::make_unique<ResetEffectCue>(); }},
        {"script",      [] { return std::make_unique<ScriptCue>(); }},
        {"record",      [] { return std::make_unique<RecordCue>(); }},
#endif
    };
    return table;
}

void CueList::fromJson(const QJsonArray &arr) {
    emit aboutToReset();
    for (auto &c : m_cues) c->stop();
    m_cues.clear();
    m_playhead = 0;
    for (const auto &val : arr) {
        const auto obj  = val.toObject();
        const QString t = obj["cueType"].toString();
        const auto &factories = cueFactories();
        const auto it = factories.find(t);
        if (it == factories.end()) continue;

        std::unique_ptr<Cue> cue = (*it)();
        cue->fromJson(obj);
        connectCue(cue.get());
        m_cues.push_back(std::move(cue));
    }
    emit listReset();
}
