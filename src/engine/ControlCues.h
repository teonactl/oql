#pragma once
#include "Cue.h"
#include "PluginChain.h"
#include <QTimer>

// ── ControlCue: abstract base for Stop / Fade / Pause ────────────────────────
class ControlCue : public Cue {
    Q_OBJECT
public:
    explicit ControlCue(QObject *parent = nullptr) : Cue(parent) {}

    QString targetId() const { return m_targetId; }
    void    setTargetId(const QString &id) { m_targetId = id; emit propertyChanged(); }
    void    setTarget(Cue *t) { m_target = t; }

    void   pause()          override {}
    double position() const override { return 0.0; }

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

protected:
    QString m_targetId;
    Cue    *m_target = nullptr;
};

// ── StopCue ───────────────────────────────────────────────────────────────────
class StopCue : public ControlCue {
    Q_OBJECT
public:
    explicit StopCue(QObject *parent = nullptr) : ControlCue(parent) {}

    Type    type()     const override { return Type::Stop; }
    QString typeName() const override { return tr("Stop"); }
    void    go()             override;
    void    stop()           override { setState(State::Idle); }
    double  duration() const override { return 0.0; }

    QJsonObject toJson() const override;
};

// ── FadeCue ───────────────────────────────────────────────────────────────────
class FadeCue : public ControlCue {
    Q_OBJECT
public:
    explicit FadeCue(QObject *parent = nullptr);

    Type    type()     const override { return Type::Fade; }
    QString typeName() const override { return tr("Fade"); }
    void    go()             override;
    void    stop()           override;
    double  duration() const override { return m_fadeDuration; }
    double  position() const override { return m_elapsed; }

    double targetVolume()  const { return m_targetVolume; }
    double fadeDuration()  const { return m_fadeDuration; }
    bool   stopAtEnd()     const { return m_stopAtEnd; }
    void   setTargetVolume(double v) { m_targetVolume = qBound(0.0,v,1.0); emit propertyChanged(); }
    void   setFadeDuration(double s) { m_fadeDuration = qMax(0.01, s); emit propertyChanged(); }
    void   setStopAtEnd(bool v)      { m_stopAtEnd = v; emit propertyChanged(); }

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private slots:
    void onTick();

private:
    QTimer *m_timer;
    double  m_targetVolume = 0.0;
    double  m_fadeDuration = 2.0;
    double  m_startVolume  = 1.0;
    double  m_elapsed      = 0.0;
    bool    m_stopAtEnd    = false;
};

// ── PauseCue ──────────────────────────────────────────────────────────────────
class PauseCue : public ControlCue {
    Q_OBJECT
public:
    explicit PauseCue(QObject *parent = nullptr) : ControlCue(parent) {}

    Type    type()     const override { return Type::Pause; }
    QString typeName() const override { return tr("Pausa"); }
    void    go()             override;
    void    stop()           override { setState(State::Idle); }
    double  duration() const override { return 0.0; }

    QJsonObject toJson() const override;
};

// ── PlayCue ───────────────────────────────────────────────────────────────────
class PlayCue : public ControlCue {
    Q_OBJECT
public:
    explicit PlayCue(QObject *parent = nullptr) : ControlCue(parent) {}

    Type    type()     const override { return Type::Play; }
    QString typeName() const override { return tr("Play"); }
    void    go()             override;
    void    stop()           override { setState(State::Idle); }
    double  duration() const override { return 0.0; }

    QJsonObject toJson() const override;
};

// ── SpeedCue ──────────────────────────────────────────────────────────────────
class SpeedCue : public ControlCue {
    Q_OBJECT
public:
    explicit SpeedCue(double rate = 1.5, QObject *parent = nullptr)
        : ControlCue(parent), m_rate(rate) {}

    Type    type()     const override { return Type::Speed; }
    QString typeName() const override { return m_rate >= 1.0 ? tr("Velocizza") : tr("Rallenta"); }
    void    go()             override;
    void    stop()           override { setState(State::Idle); }
    double  duration() const override { return 0.0; }

    double rate()       const { return m_rate; }
    void   setRate(double r)  { m_rate = qBound(0.1, r, 10.0); emit propertyChanged(); }

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private:
    double m_rate = 1.5;
};

// ── EffectCue: applica una catena di plugin a un AudioCue target ──────────────
class EffectCue : public ControlCue {
    Q_OBJECT
public:
    explicit EffectCue(QObject *parent = nullptr);

    Type    type()     const override { return Type::Effect; }
    QString typeName() const override { return tr("Effetto"); }
    void    go()             override;
    void    stop()           override;
    double  duration() const override { return m_duration; }
    double  position() const override;

    double effectDuration()   const { return m_duration; }
    void setEffectDuration(double s){ m_duration = qMax(0.0, s); emit propertyChanged(); }

    PluginChain       *pluginChain()       { return &m_chain; }
    const PluginChain *pluginChain() const { return &m_chain; }

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private slots:
    void onTimeout();
    // Called when AudioCue::pluginSnapshotRestored() fires (e.g. from ResetEffectCue).
    // Transitions the EffectCue to Idle without calling restorePluginSnapshot() again.
    void handleExternalReset();

private:
    PluginChain    m_chain;
    QTimer        *m_timer        = nullptr;
    double         m_duration     = 0.0;
    Cue           *m_activeTarget = nullptr;
    QElapsedTimer  m_startTimer;
};

// ── ResetEffectCue: ripristina il plugin chain originale del target AudioCue ──
class ResetEffectCue : public ControlCue {
    Q_OBJECT
public:
    explicit ResetEffectCue(QObject *parent = nullptr) : ControlCue(parent) {}

    Type    type()     const override { return Type::ResetEffect; }
    QString typeName() const override { return tr("Reset Effetti"); }
    void    go()             override;
    void    stop()           override { setState(State::Idle); }
    double  duration() const override { return 0.0; }

    QJsonObject toJson() const override;
};
