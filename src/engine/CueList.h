#pragma once
#include "Cue.h"
#include <QObject>
#include <QJsonArray>
#include <vector>
#include <memory>

class CueList : public QObject {
    Q_OBJECT
public:
    explicit CueList(QObject *parent = nullptr);

    void addCue(std::unique_ptr<Cue> cue, int index = -1);
    void removeCue(int index);
    void moveCue(int from, int to);

    Cue* cueAt(int index)        const;
    Cue* findCueById(const QString &id) const;
    int  count()                 const { return int(m_cues.size()); }
    int  playheadIndex()     const { return m_playhead; }
    void setPlayhead(int index);

    void go();
    void stopAll();
    void pauseAll();

    QJsonArray toJson()             const;
    void       fromJson(const QJsonArray &arr);

signals:
    void aboutToReset();
    void listReset();
    void cueAdded(int index);
    void cueRemoved(int index);
    void cueMoved(int from, int to);
    void playheadChanged(int index);
    void cueStateChanged(int index, Cue::State state);
    void cuePropertyChanged(int index);
    void cueLayoutChanged(int index); // group collapse: view rebuild only, not workspace modified

private:
    void connectCue(Cue *cue);
    int  nextNonLabel(int from) const;
    int  nextInSequence(int from) const; // respects group membership

    std::vector<std::unique_ptr<Cue>> m_cues;
    int m_playhead = 0;
};
