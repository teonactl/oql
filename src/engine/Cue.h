#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QElapsedTimer>

class Cue : public QObject {
    Q_OBJECT
public:
    enum class Type  { Audio, Video, Stop, Fade, Pause, Speed, Play, Mic, Group, Label, Text, Effect, ResetEffect };
    enum class State { Idle, Waiting, Playing, Paused };

    explicit Cue(QObject *parent = nullptr);
    virtual ~Cue() = default;

    QString id()            const { return m_id; }
    QString number()        const { return m_number; }
    QString name()          const { return m_name; }
    QString notes()         const { return m_notes; }
    QString parentGroupId() const { return m_parentGroupId; }

    void setNumber(const QString &v)        { m_number        = v; emit propertyChanged(); }
    void setName(const QString &v)          { m_name          = v; emit propertyChanged(); }
    void setNotes(const QString &v)         { m_notes         = v; emit propertyChanged(); }
    void setParentGroupId(const QString &v) { m_parentGroupId = v; emit propertyChanged(); }

    double preWait()   const { return m_preWait; }
    double postWait()  const { return m_postWait; }
    bool autoContinue() const { return m_autoContinue; }
    bool autoFollow()   const { return m_autoFollow; }

    void setPreWait(double s)      { m_preWait      = s; emit propertyChanged(); }
    void setPostWait(double s)     { m_postWait     = s; emit propertyChanged(); }
    void setAutoContinue(bool v)   { m_autoContinue = v; emit propertyChanged(); }
    void setAutoFollow(bool v)     { m_autoFollow   = v; emit propertyChanged(); }

    State state() const { return m_state; }

    // Pre-wait countdown helpers (valid only when state == Waiting)
    double waitTotal()   const { return m_waitTotal; }
    double waitElapsed() const {
        return m_waitTimer.isValid() ? m_waitTimer.elapsed() / 1000.0 : 0.0;
    }
    void beginPreWait(double seconds) {
        m_waitTotal = seconds;
        m_waitTimer.start();
        setState(State::Waiting);
    }

    virtual Type    type()          const = 0;
    virtual QString typeName()      const = 0;
    virtual void    go()                  = 0;
    virtual void    stop()                = 0;
    virtual void    pause()               = 0;
    virtual double  duration()      const = 0;
    virtual double  position()      const { return 0.0; }
    virtual double  volume()              const { return 1.0; }
    virtual void    setVolume(double)           {}
    virtual void    setPlaybackVolume(double v) { setVolume(v); }  // affects output only, not m_volume
    virtual void    setPlaybackRate(double)     {}

    virtual QJsonObject toJson()                  const;
    virtual void        fromJson(const QJsonObject &o);

signals:
    void stateChanged(Cue::State state);
    void finished();
    void propertyChanged();
    void displayChanged(); // UI refresh only — does NOT mark workspace as modified

protected:
    void setState(State s);

    QString m_id;
    QString m_number;
    QString m_name;
    QString m_notes;
    QString m_parentGroupId;
    double  m_preWait      = 0.0;
    double  m_postWait     = 0.0;
    bool    m_autoContinue = false;
    bool    m_autoFollow   = false;
    State   m_state        = State::Idle;

    double        m_waitTotal = 0.0;
    QElapsedTimer m_waitTimer;
};
