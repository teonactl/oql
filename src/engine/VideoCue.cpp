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
    m_audioOutput->setVolume(float(qBound(0.0, v, 1.0)));
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

    m_player->play();
    setState(State::Playing);
}

void VideoCue::stop() {
    m_restarting = true;
    m_player->stop();
    m_restarting = false;
    m_audioOutput->setVolume(float(m_volume));
    setState(State::Idle);
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
        setState(State::Idle);
        emit finished();
    }
}

QJsonObject VideoCue::toJson() const {
    auto obj         = Cue::toJson();
    obj["cueType"]   = "video";
    obj["filePath"]  = m_filePath;
    obj["volume"]    = m_volume;
    obj["loopCount"] = m_loopCount;
    return obj;
}

void VideoCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    const QString path = o["filePath"].toString();
    if (!path.isEmpty()) setFilePath(path);
    setVolume(o["volume"].toDouble(1.0));
    m_loopCount = o["loopCount"].toInt(1);
}
