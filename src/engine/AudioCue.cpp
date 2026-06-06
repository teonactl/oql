#include "AudioCue.h"
#include <QFileInfo>
#include <QUrl>
#include <QJsonArray>

AudioCue::AudioCue(QObject *parent) : Cue(parent) {
    m_player          = new QMediaPlayer(this);
    m_audioOutput     = new QAudioOutput(this);
    m_fadeTimer       = new QTimer(this);
    m_automationTimer = new QTimer(this);

    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(float(m_volume));
    m_fadeTimer->setInterval(20);
    m_automationTimer->setInterval(50);

    connect(m_player,    &QMediaPlayer::playbackStateChanged,
            this,        &AudioCue::onPlaybackStateChanged);
    connect(m_fadeTimer, &QTimer::timeout,
            this,        &AudioCue::onFadeTick);
    connect(m_automationTimer, &QTimer::timeout,
            this,               &AudioCue::onAutomationTick);
    connect(m_player, &QMediaPlayer::durationChanged,
            this,     [this](qint64) { emit propertyChanged(); });
}

void AudioCue::setFilePath(const QString &path) {
    m_filePath = path;
    m_player->setSource(QUrl::fromLocalFile(path));
    if (m_name.isEmpty())
        m_name = QFileInfo(path).completeBaseName();
    emit propertyChanged();
}

void AudioCue::setVolume(double v) {
    m_volume = qBound(0.0, v, 1.0);
    if (!m_fadingOut)
        m_audioOutput->setVolume(float(m_volume * m_playbackScale));
    emit propertyChanged();
}

void AudioCue::setPlaybackVolume(double v) {
    const double clamped = qBound(0.0, v, 1.0);
    m_playbackScale = (m_volume > 0.001) ? clamped / m_volume : (clamped > 0.001 ? 1.0 : 0.0);
    m_audioOutput->setVolume(float(clamped));
}

void AudioCue::go() {
    if (m_state == State::Paused) {
        m_player->play();
        m_automationTimer->start();
        setState(State::Playing);
        return;
    }
    m_fadeTimer->stop();
    m_automationTimer->stop();
    m_fadingOut     = false;
    m_playbackScale = 1.0;
    m_loopsRemaining = m_loopCount;

    m_restarting = true;
    m_player->stop();
    m_restarting = false;
    m_player->setPlaybackRate(1.0);

    if (!m_volumePoints.isEmpty()) {
        m_currentVol = float(m_volume * computeFadeVol(0.0));
        m_audioOutput->setVolume(float(m_currentVol));
    } else {
        m_currentVol = (m_fadeIn > 0.01) ? 0.0f : float(m_volume);
        m_audioOutput->setVolume(float(m_currentVol));
        if (m_fadeIn > 0.01) {
            m_fadeStep = m_volume / (m_fadeIn * 50.0);
            m_fadeTimer->start();
        }
    }

    m_automationTimer->start();

    if (m_trimStart > 0.001)
        m_player->setPosition(qint64(m_trimStart * 1000.0));

    m_player->play();
    setState(State::Playing);
}

void AudioCue::stop() {
    m_fadeTimer->stop();
    m_automationTimer->stop();
    m_fadingOut     = false;
    m_playbackScale = 1.0;
    m_restarting    = true;
    m_player->stop();
    m_restarting = false;
    m_audioOutput->setVolume(float(m_volume));
    setState(State::Idle);
}

void AudioCue::pause() {
    m_fadeTimer->stop();
    m_automationTimer->stop();
    m_player->pause();
    setState(State::Paused);
}

double AudioCue::duration() const { return m_player->duration() / 1000.0; }
double AudioCue::position() const { return m_player->position() / 1000.0; }

void AudioCue::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::StoppedState && m_state == State::Playing && !m_restarting) {
        if (shouldLoop()) {
            if (m_loopCount > 0) --m_loopsRemaining;
            restartForLoop();
            return;
        }
        m_fadeTimer->stop();
        m_automationTimer->stop();
        m_fadingOut     = false;
        m_playbackScale = 1.0;
        m_audioOutput->setVolume(float(m_volume));
        setState(State::Idle);
        emit finished();
    }
}

void AudioCue::onAutomationTick() {
    if (m_state != State::Playing) return;
    const double durMs = m_player->duration();
    if (durMs <= 0) return;
    const double t   = qBound(0.0, m_player->position() / durMs, 1.0);
    const double pos = m_player->position() / 1000.0;

    // Trim end: stop or loop when position reaches m_trimEnd
    if (m_trimEnd > 0.001 && pos >= m_trimEnd) {
        if (shouldLoop()) {
            if (m_loopCount > 0) --m_loopsRemaining;
            restartForLoop();
            return;
        }
        m_automationTimer->stop();
        m_fadeTimer->stop();
        m_fadingOut     = false;
        m_playbackScale = 1.0;
        m_restarting    = true;
        m_player->stop();
        m_restarting = false;
        m_audioOutput->setVolume(float(m_volume));
        setState(State::Idle);
        emit finished();
        return;
    }

    // Volume automation — always multiplied by m_playbackScale so FadeCue's effect is preserved
    if (!m_volumePoints.isEmpty()) {
        const double vol = m_volume * m_playbackScale * interpolateVolume(t) * computeFadeVol(t);
        m_audioOutput->setVolume(float(vol));
    } else if (m_fadeOut > 0.01) {
        const double durS = m_player->duration() / 1000.0;
        const double remaining = durS > 0 ? durS * (1.0 - t) : 1.0;
        const double fadeOutFactor = qMin(1.0, remaining / m_fadeOut);
        m_audioOutput->setVolume(float(m_currentVol * m_playbackScale * fadeOutFactor));
    }
}

bool AudioCue::shouldLoop() const {
    return m_loopCount == 0 || m_loopsRemaining > 1;
}

void AudioCue::restartForLoop() {
    m_fadeTimer->stop();
    m_fadingOut     = false;
    m_playbackScale = 1.0;
    m_restarting    = true;
    m_player->stop();
    m_restarting = false;

    if (!m_volumePoints.isEmpty()) {
        m_currentVol = float(m_volume * computeFadeVol(0.0));
        m_audioOutput->setVolume(float(m_currentVol));
    } else {
        m_currentVol = (m_fadeIn > 0.01) ? 0.0f : float(m_volume);
        m_audioOutput->setVolume(float(m_currentVol));
        if (m_fadeIn > 0.01) {
            m_fadeStep = m_volume / (m_fadeIn * 50.0);
            m_fadeTimer->start();
        }
    }
    if (m_trimStart > 0.001)
        m_player->setPosition(qint64(m_trimStart * 1000.0));
    m_automationTimer->start();
    m_player->play();
}

double AudioCue::interpolateVolume(double t) const {
    if (m_volumePoints.isEmpty()) return 1.0;
    if (t <= m_volumePoints.first().x()) return m_volumePoints.first().y();
    if (t >= m_volumePoints.last().x())  return m_volumePoints.last().y();
    for (int i = 0; i + 1 < m_volumePoints.size(); ++i) {
        const double x0 = m_volumePoints[i].x(), x1 = m_volumePoints[i+1].x();
        if (t >= x0 && t <= x1) {
            const double a = (t - x0) / (x1 - x0);
            return m_volumePoints[i].y() * (1.0 - a) + m_volumePoints[i+1].y() * a;
        }
    }
    return 1.0;
}

double AudioCue::computeFadeVol(double t) const {
    const double durS = m_player->duration() / 1000.0;
    if (durS <= 0) return 1.0;
    double v = 1.0;
    if (m_fadeIn  > 0.01) v *= qMin(1.0, t * durS / m_fadeIn);
    if (m_fadeOut > 0.01) v *= qMin(1.0, (1.0 - t) * durS / m_fadeOut);
    return v;
}

void AudioCue::onFadeTick() {
    if (m_fadingOut) {
        m_currentVol -= m_fadeStep;
        if (m_currentVol <= 0.0) {
            m_currentVol = 0.0;
            m_audioOutput->setVolume(0.0f);
            m_fadeTimer->stop();
            m_player->stop();   // triggers onPlaybackStateChanged → sets Idle + emits finished
            return;
        }
    } else {
        m_currentVol += m_fadeStep;
        if (m_currentVol >= m_volume) {
            m_currentVol = m_volume;
            m_fadeTimer->stop();
        }
    }
    m_audioOutput->setVolume(float(m_currentVol * m_playbackScale));
}

QJsonObject AudioCue::toJson() const {
    auto obj        = Cue::toJson();
    obj["cueType"]  = "audio";
    obj["filePath"] = m_filePath;
    obj["volume"]   = m_volume;
    obj["fadeIn"]   = m_fadeIn;
    obj["fadeOut"]  = m_fadeOut;
    obj["trimStart"] = m_trimStart;
    obj["trimEnd"]   = m_trimEnd;
    obj["channel"]   = int(m_channel);
    obj["loopCount"] = m_loopCount;

    QJsonArray pts;
    for (const auto &p : m_volumePoints)
        pts.append(QJsonArray{p.x(), p.y()});
    obj["volumePoints"] = pts;

    return obj;
}

void AudioCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    const QString path = o["filePath"].toString();
    if (!path.isEmpty()) setFilePath(path);
    setVolume(o["volume"].toDouble(1.0));
    m_fadeIn    = o["fadeIn"].toDouble(3.0);
    m_fadeOut   = o["fadeOut"].toDouble(3.0);
    m_trimStart = o["trimStart"].toDouble(0.0);
    m_trimEnd   = o["trimEnd"].toDouble(0.0);
    m_channel   = ChannelRoute(o["channel"].toInt(0));
    m_loopCount = o["loopCount"].toInt(1);

    m_volumePoints.clear();
    for (const auto &v : o["volumePoints"].toArray()) {
        const auto arr = v.toArray();
        if (arr.size() == 2)
            m_volumePoints.append(QPointF(arr[0].toDouble(), arr[1].toDouble()));
    }
}
