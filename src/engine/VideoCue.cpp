#include "VideoCue.h"
#include <QFileInfo>
#include <QUrl>
#include <QVideoSink>

VideoCue::VideoCue(QObject *parent) : Cue(parent) {
    m_player      = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(float(m_volume));

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this,     &VideoCue::onPlaybackStateChanged);
}

void VideoCue::setFilePath(const QString &path) {
    m_filePath = path;
    m_player->setSource(QUrl::fromLocalFile(path));
    if (m_name.isEmpty())
        m_name = QFileInfo(path).completeBaseName();
    emit propertyChanged();
}

void VideoCue::setVolume(double v) {
    m_volume = qBound(0.0, v, 1.0);
    m_audioOutput->setVolume(float(m_volume));
    emit propertyChanged();
}

void VideoCue::setPlaybackVolume(double v) {
    const double clamped = qBound(0.0, v, 1.0);
    m_audioOutput->setVolume(float(clamped));
    // Normalizza rispetto al volume base (come AudioCue::m_playbackScale) cosi
    // un fade arriva a opacita visiva 0 anche se il volume base e' < 1.0.
    m_visualLevel = (m_volume > 0.001)
        ? qBound(0.0, 1.0, clamped / m_volume)
        : (clamped > 0.001 ? 1.0 : 0.0);
    emit displayChanged();
}

void VideoCue::setVideoSink(QVideoSink *sink) {
    m_player->setVideoSink(sink);
}

void VideoCue::setLoopCount(int n) {
    m_loopCount = qMax(0, n);
    emit propertyChanged();
}

void VideoCue::setPlaybackRate(double r) {
    m_player->setPlaybackRate(r);
}

void VideoCue::go() {
    if (m_state == State::Paused) {
        m_player->play();
        setState(State::Playing);
        return;
    }
    m_player->setLoops(m_loopCount == 0 ? QMediaPlayer::Infinite : m_loopCount);
    m_player->setPlaybackRate(1.0);
    m_restarting = true;
    m_player->stop();
    m_restarting = false;

    m_visualLevel = 1.0;
    m_player->play();
    setState(State::Playing);
    emit displayChanged();
}

void VideoCue::stop() {
    m_restarting = true;
    m_player->stop();
    m_restarting = false;
    m_audioOutput->setVolume(float(m_volume));
    m_visualLevel = 1.0;
    setState(State::Idle);
    emit displayChanged();
}

void VideoCue::pause() {
    m_player->pause();
    setState(State::Paused);
}

double VideoCue::duration() const { return m_player->duration() / 1000.0; }
double VideoCue::position() const { return m_player->position() / 1000.0; }

void VideoCue::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::StoppedState && m_state == State::Playing && !m_restarting) {
        m_audioOutput->setVolume(float(m_volume));
        m_visualLevel = 1.0;
        setState(State::Idle);
        emit displayChanged();
        emit finished();
    }
}

QJsonObject VideoCue::toJson() const {
    auto obj         = Cue::toJson();
    obj["cueType"]   = "video";
    obj["filePath"]  = m_filePath;
    obj["volume"]    = m_volume;
    obj["loopCount"] = m_loopCount;
    obj["background"]      = static_cast<int>(m_background);
    obj["backgroundImage"] = m_backgroundImagePath;
    return obj;
}

void VideoCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    const QString path = o["filePath"].toString();
    if (!path.isEmpty()) setFilePath(path);
    setVolume(o["volume"].toDouble(1.0));
    m_loopCount = o["loopCount"].toInt(1);
    m_background = static_cast<Background>(o["background"].toInt(
                        static_cast<int>(Background::Black)));
    m_backgroundImagePath = o["backgroundImage"].toString();
}
