#include "ControlCues.h"
#include "AudioCue.h"
#include <QJsonObject>

// ── SpeedCue ──────────────────────────────────────────────────────────────────

// ── ControlCue ────────────────────────────────────────────────────────────────

QJsonObject ControlCue::toJson() const {
    auto obj        = Cue::toJson();
    obj["targetId"] = m_targetId;
    return obj;
}

void ControlCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_targetId = o["targetId"].toString();
}

// ── StopCue ───────────────────────────────────────────────────────────────────

void StopCue::go() {
    if (m_target) m_target->stop();
    setState(State::Idle);
    emit finished();
}

QJsonObject StopCue::toJson() const {
    auto obj       = ControlCue::toJson();
    obj["cueType"] = "stop";
    return obj;
}

// ── FadeCue ───────────────────────────────────────────────────────────────────

FadeCue::FadeCue(QObject *parent) : ControlCue(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &FadeCue::onTick);
}

void FadeCue::go() {
    if (!m_target) { setState(State::Idle); emit finished(); return; }
    m_startVolume = m_target->volume();
    m_elapsed = 0.0;
    setState(State::Playing);
    m_timer->start();
}

void FadeCue::stop() {
    m_timer->stop();
    setState(State::Idle);
}

void FadeCue::onTick() {
    if (!m_target) { stop(); emit finished(); return; }
    m_elapsed += 0.05;
    const double progress = qMin(m_elapsed / m_fadeDuration, 1.0);
    const double vol = m_startVolume + (m_targetVolume - m_startVolume) * progress;
    m_target->setPlaybackVolume(vol);
    if (progress >= 1.0) {
        m_timer->stop();
        if (m_stopAtEnd)
            m_target->stop();  // stop() restores m_volume on the target
        setState(State::Idle);
        emit finished();
    }
}

QJsonObject FadeCue::toJson() const {
    auto obj               = ControlCue::toJson();
    obj["cueType"]         = "fade";
    obj["targetVolume"]    = m_targetVolume;
    obj["fadeDuration"]    = m_fadeDuration;
    obj["stopAtEnd"]       = m_stopAtEnd;
    return obj;
}

void FadeCue::fromJson(const QJsonObject &o) {
    ControlCue::fromJson(o);
    m_targetVolume = o["targetVolume"].toDouble(0.0);
    m_fadeDuration = o["fadeDuration"].toDouble(2.0);
    m_stopAtEnd    = o["stopAtEnd"].toBool(false);
}

// ── PauseCue ──────────────────────────────────────────────────────────────────

void PauseCue::go() {
    if (m_target) m_target->pause();
    setState(State::Idle);
    emit finished();
}

QJsonObject PauseCue::toJson() const {
    auto obj       = ControlCue::toJson();
    obj["cueType"] = "pause";
    return obj;
}

// ── PlayCue ───────────────────────────────────────────────────────────────────

void PlayCue::go() {
    if (m_target) m_target->go();  // resumes if paused, restarts if idle
    setState(State::Idle);
    emit finished();
}

QJsonObject PlayCue::toJson() const {
    auto obj       = ControlCue::toJson();
    obj["cueType"] = "play";
    return obj;
}

// ── SpeedCue ──────────────────────────────────────────────────────────────────

void SpeedCue::go() {
    if (m_target) m_target->setPlaybackRate(m_rate);
    setState(State::Idle);
    emit finished();
}

QJsonObject SpeedCue::toJson() const {
    auto obj       = ControlCue::toJson();
    obj["cueType"] = "speed";
    obj["rate"]    = m_rate;
    return obj;
}

void SpeedCue::fromJson(const QJsonObject &o) {
    ControlCue::fromJson(o);
    m_rate = o["rate"].toDouble(1.5);
}

// ── EffectCue ─────────────────────────────────────────────────────────────────

EffectCue::EffectCue(QObject *parent) : ControlCue(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &EffectCue::onTimeout);
}

void EffectCue::go() {
    auto *ac = dynamic_cast<AudioCue*>(m_target);
    if (!ac) { setState(State::Idle); emit finished(); return; }

    m_timer->stop();  // cancel any pending auto-reset from a previous go()

    if (!ac->hasPluginSnapshot())
        ac->savePluginSnapshot();
    ac->applyPluginChain(m_chain.toJson());

    if (m_duration > 0.001) {
        m_activeTarget = m_target;
        m_startTimer.start();
        setState(State::Playing);
        m_timer->start(int(m_duration * 1000));
    } else {
        setState(State::Idle);
        emit finished();
    }
}

void EffectCue::stop() {
    m_timer->stop();
    m_startTimer.invalidate();
    if (m_state == State::Playing && m_activeTarget) {
        if (auto *ac = dynamic_cast<AudioCue*>(m_activeTarget))
            ac->restorePluginSnapshot();
        m_activeTarget = nullptr;
    }
    setState(State::Idle);
}

void EffectCue::onTimeout() {
    m_startTimer.invalidate();
    if (auto *ac = dynamic_cast<AudioCue*>(m_activeTarget))
        ac->restorePluginSnapshot();
    m_activeTarget = nullptr;
    setState(State::Idle);
    emit finished();
}

double EffectCue::position() const {
    if (m_duration <= 0.001 || !m_startTimer.isValid()) return 0.0;
    return qMin(m_startTimer.elapsed() / 1000.0, m_duration);
}

QJsonObject EffectCue::toJson() const {
    auto obj          = ControlCue::toJson();
    obj["cueType"]    = "effect";
    obj["chain"]      = m_chain.toJson();
    obj["duration"]   = m_duration;
    return obj;
}

void EffectCue::fromJson(const QJsonObject &o) {
    ControlCue::fromJson(o);
    m_chain.fromJson(o["chain"].toArray());
    m_duration = o["duration"].toDouble(0.0);
}

// ── ResetEffectCue ────────────────────────────────────────────────────────────

void ResetEffectCue::go() {
    auto *ac = dynamic_cast<AudioCue*>(m_target);
    if (!ac) { setState(State::Idle); emit finished(); return; }
    ac->restorePluginSnapshot();
    setState(State::Idle);
    emit finished();
}

QJsonObject ResetEffectCue::toJson() const {
    auto obj       = ControlCue::toJson();
    obj["cueType"] = "reseteffect";
    return obj;
}
