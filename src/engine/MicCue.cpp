#include "MicCue.h"
#include <QMediaDevices>
#include <QAudioFormat>
#include <QJsonObject>

MicCue::MicCue(QObject *parent) : Cue(parent) {}

MicCue::~MicCue() { teardown(); }

void MicCue::teardown() {
    if (m_source) { m_source->stop(); delete m_source; m_source = nullptr; }
    if (m_sink)   { m_sink->stop();   delete m_sink;   m_sink   = nullptr; }
    m_ioDevice = nullptr;
}

void MicCue::setInputDeviceId(const QString &id) {
    m_inputDeviceId = id;
    emit propertyChanged();
}

void MicCue::setVolume(double v) {
    m_volume = qBound(0.0, v, 1.0);
    if (m_sink) m_sink->setVolume(float(m_volume));
    emit propertyChanged();
}

void MicCue::setPlaybackVolume(double v) {
    if (m_sink) m_sink->setVolume(float(qBound(0.0, v, 1.0)));
}

void MicCue::go() {
    if (m_state == State::Paused) {
        if (m_sink) m_sink->setVolume(float(m_volume));
        setState(State::Playing);
        return;
    }
    teardown();

    // Resolve input device
    QAudioDevice inputDev = QMediaDevices::defaultAudioInput();
    if (!m_inputDeviceId.isEmpty()) {
        for (const auto &dev : QMediaDevices::audioInputs()) {
            if (dev.id() == m_inputDeviceId) { inputDev = dev; break; }
        }
    }
    if (inputDev.isNull()) { setState(State::Idle); emit finished(); return; }

    // Use the input device's preferred format for lowest latency / best compat
    QAudioFormat fmt = inputDev.preferredFormat();

    m_source = new QAudioSource(inputDev, fmt, this);
    m_source->setBufferSize(2048);

    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), fmt, this);
    m_sink->setVolume(float(m_volume));
    m_sink->setBufferSize(2048);

    m_ioDevice = m_source->start();
    if (m_ioDevice)
        m_sink->start(m_ioDevice);

    setState(State::Playing);
}

void MicCue::stop() {
    teardown();
    setState(State::Idle);
}

void MicCue::pause() {
    if (m_state == State::Playing && m_sink) {
        m_sink->setVolume(0.0f);
        setState(State::Paused);
    }
}

QJsonObject MicCue::toJson() const {
    auto obj             = Cue::toJson();
    obj["cueType"]       = "mic";
    obj["volume"]        = m_volume;
    obj["inputDeviceId"] = m_inputDeviceId;
    return obj;
}

void MicCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_volume        = qBound(0.0, o["volume"].toDouble(1.0), 1.0);
    m_inputDeviceId = o["inputDeviceId"].toString();
}
