#include "RecordCue.h"
#include "CueList.h"
#include "AudioCue.h"
#include <cmath>
#include <QMediaDevices>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>

RecordCue::RecordCue(QObject *parent) : Cue(parent) {}

RecordCue::~RecordCue() {
    if (m_source) { m_source->stop(); delete m_source; }
}

void RecordCue::go() {
    if (m_state == State::Playing)
        stopRecording();
    else
        startRecording();
}

void RecordCue::stop() {
    if (m_state == State::Playing)
        stopRecording();
    setState(State::Idle);
}

void RecordCue::startRecording() {
    if (m_source) { m_source->stop(); delete m_source; m_source = nullptr; }
    m_buffer.clear();

    QAudioDevice inputDev = QMediaDevices::defaultAudioInput();
    if (!m_inputDeviceId.isEmpty()) {
        for (const auto &dev : QMediaDevices::audioInputs()) {
            if (dev.id() == m_inputDeviceId) { inputDev = dev; break; }
        }
    }
    if (inputDev.isNull()) { setState(State::Idle); return; }

    // Prefer 16-bit mono 44100; fall back to device preferred
    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!inputDev.isFormatSupported(fmt))
        fmt = inputDev.preferredFormat();
    m_format = fmt;

    m_source = new QAudioSource(inputDev, fmt, this);
    m_source->setBufferSize(8192);
    m_ioDevice = m_source->start();
    if (!m_ioDevice) { delete m_source; m_source = nullptr; setState(State::Idle); return; }

    connect(m_ioDevice, &QIODevice::readyRead, this, &RecordCue::onAudioDataReady);
    setState(State::Playing);
}

void RecordCue::onAudioDataReady() {
    if (!m_ioDevice) return;
    const QByteArray chunk = m_ioDevice->readAll();
    m_buffer.append(chunk);

    // Compute peak for the VU meter
    float peak = 0.0f;
    if (m_format.sampleFormat() == QAudioFormat::Int16) {
        const auto *s = reinterpret_cast<const int16_t*>(chunk.constData());
        const int n = chunk.size() / int(sizeof(int16_t));
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(s[i]) / 32768.0f);
    } else if (m_format.sampleFormat() == QAudioFormat::Float) {
        const auto *s = reinterpret_cast<const float*>(chunk.constData());
        const int n = chunk.size() / int(sizeof(float));
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(s[i]));
    }
    m_inputLevel = peak;
}

void RecordCue::stopRecording() {
    if (!m_source) { setState(State::Idle); return; }

    if (m_ioDevice) {
        m_buffer.append(m_ioDevice->readAll());
        disconnect(m_ioDevice, nullptr, this, nullptr);
        m_ioDevice = nullptr;
    }
    m_source->stop();
    delete m_source;
    m_source = nullptr;

    const QString path = makeOutputPath();
    writeWavFile(path);
    m_lastRecordingPath = path;

    // Push recording to linked AudioCue via signal — CueList handles the routing
    if (!m_linkedAudioCueId.isEmpty())
        emit requestSetFilePath(m_linkedAudioCueId, path);

    m_inputLevel = 0.0f;
    setState(State::Idle);
    emit recordingFinished(path);
    emit finished();
}

void RecordCue::writeWavFile(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;

    // Convert to 16-bit PCM if needed
    QByteArray pcm16;
    if (m_format.sampleFormat() == QAudioFormat::Int16) {
        pcm16 = m_buffer;
    } else if (m_format.sampleFormat() == QAudioFormat::Float) {
        const int n = m_buffer.size() / int(sizeof(float));
        pcm16.resize(n * 2);
        const float   *src = reinterpret_cast<const float*>(m_buffer.constData());
        int16_t       *dst = reinterpret_cast<int16_t*>(pcm16.data());
        for (int i = 0; i < n; ++i)
            dst[i] = int16_t(qBound(-32767.0f, src[i] * 32767.0f, 32767.0f));
    } else {
        pcm16 = m_buffer;
    }

    const uint32_t sr         = uint32_t(m_format.sampleRate());
    const uint16_t channels   = uint16_t(m_format.channelCount());
    const uint16_t bps        = 16;
    const uint16_t blkAlign   = uint16_t(channels * (bps / 8));
    const uint32_t byteRate   = sr * blkAlign;
    const uint32_t dataSize   = uint32_t(pcm16.size());
    const uint32_t fileSize   = 36 + dataSize;

    auto w32 = [&](uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
    auto w16 = [&](uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };

    f.write("RIFF", 4); w32(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16);
    w16(1); w16(channels); w32(sr); w32(byteRate); w16(blkAlign); w16(bps);
    f.write("data", 4); w32(dataSize);
    f.write(pcm16);
}

QString RecordCue::makeOutputPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + "/recordings";
    QDir().mkpath(dir);
    return dir + "/rec_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wav";
}

QJsonObject RecordCue::toJson() const {
    auto obj = Cue::toJson();
    obj["cueType"]           = "record";
    obj["inputDeviceId"]     = m_inputDeviceId;
    obj["linkedAudioCueId"]  = m_linkedAudioCueId;
    obj["lastRecordingPath"] = m_lastRecordingPath;
    return obj;
}

void RecordCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    m_inputDeviceId     = o["inputDeviceId"].toString();
    m_linkedAudioCueId  = o["linkedAudioCueId"].toString();
    m_lastRecordingPath = o["lastRecordingPath"].toString();
}
