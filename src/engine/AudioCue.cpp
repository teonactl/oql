#include "AudioCue.h"
#include "miniaudio.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaObject>
#include <algorithm>
#include <cmath>

// ── Constructor / Destructor ──────────────────────────────────────────────────

AudioCue::AudioCue(QObject *parent) : Cue(parent) {
    m_decoder = new ma_decoder();
}

AudioCue::~AudioCue() {
    // Unregister from the audio engine first
    if (m_playing.load()) {
        m_playing.store(false);
        AudioEngine::instance().removeRenderer(this);
    }
    std::lock_guard<std::mutex> lock(m_decoderMtx);
    if (m_decoderOk) {
        ma_decoder_uninit(m_decoder);
        m_decoderOk = false;
    }
    delete m_decoder;
}

// ── File loading ──────────────────────────────────────────────────────────────

void AudioCue::setFilePath(const QString &path) {
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) { ma_decoder_uninit(m_decoder); m_decoderOk = false; }

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 0);
        if (ma_decoder_init_file(path.toUtf8().constData(), &cfg, m_decoder) == MA_SUCCESS) {
            m_decoderOk = true;
            ma_uint64 total = 0;
            ma_decoder_get_length_in_pcm_frames(m_decoder, &total);
            m_totalFrames = total;
            m_fileDuration = total > 0
                ? double(total) / double(m_decoder->outputSampleRate)
                : 0.0;
        }
    }
    m_filePath = path;
    if (m_name.isEmpty()) m_name = QFileInfo(path).completeBaseName();
    emit displayChanged();
    emit propertyChanged();
}

// ── Volume helpers ────────────────────────────────────────────────────────────

void AudioCue::setVolume(double v) {
    m_targetVolume = qBound(0.0, v, 1.0);
    emit propertyChanged();
}

void AudioCue::setPlaybackVolume(double v) {
    m_playbackScale = qBound(0.0, v, 2.0);
}

void AudioCue::setPlaybackRate(double /*r*/) {
    // TODO: implement via miniaudio resampler
}

// ── Playback ──────────────────────────────────────────────────────────────────

void AudioCue::go() {
    if (m_state == State::Paused) {
        m_paused.store(false);
        setState(State::Playing);
        return;
    }

    // Stop any currently playing stream
    if (m_playing.load()) {
        m_playing.store(false);
        AudioEngine::instance().removeRenderer(this);
    }

    if (!m_decoderOk) return;

    // Reset decoder to trim-start position
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        const int sr = int(m_decoder->outputSampleRate) > 0
                       ? int(m_decoder->outputSampleRate)
                       : AudioEngine::instance().sampleRate();
        const ma_uint64 startFrame = ma_uint64(m_trimStart * sr);
        ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
        m_framePos.store(startFrame);
    }

    m_loopsRemaining.store(m_loopCount > 0 ? m_loopCount : INT_MAX);
    m_renderVol    = (m_fadeIn > 0.01) ? 0.0f : float(m_targetVolume * m_playbackScale);
    m_paused.store(false);
    m_seekTarget.store(-1);

    // Preallocate working buffers
    const int block = AudioEngine::instance().blockSize();
    m_decodeBuf.resize(block * 2);
    m_plugL.resize(block);
    m_plugR.resize(block);

    // Prepare plugin chain
    m_chain.prepare(AudioEngine::instance().sampleRate(), block);

    m_playing.store(true);
    AudioEngine::instance().addRenderer(this);
    setState(State::Playing);
}

void AudioCue::stop() {
    if (!m_playing.load() && m_state == State::Idle) return;
    m_playing.store(false);
    m_paused.store(false);
    AudioEngine::instance().removeRenderer(this);

    // Seek back to start so position() returns 0
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) {
            ma_decoder_seek_to_pcm_frame(m_decoder, 0);
            m_framePos.store(0);
        }
    }
    setState(State::Idle);
}

void AudioCue::pause() {
    if (m_state != State::Playing) return;
    m_paused.store(true);
    setState(State::Paused);
}

double AudioCue::duration() const {
    return m_fileDuration;
}

double AudioCue::position() const {
    if (!m_decoderOk) return 0.0;
    const int sr = m_decoder->outputSampleRate > 0
                   ? int(m_decoder->outputSampleRate)
                   : AudioEngine::instance().sampleRate();
    return double(m_framePos.load()) / double(sr);
}

// ── Audio rendering (audio thread) ───────────────────────────────────────────

bool AudioCue::renderAudio(float *out, int frames, int sampleRate) {
    if (m_paused.load()) {
        // Silence, but stay active
        return true;
    }

    // Handle pending seek (from main thread: not currently triggered, reserved for future)
    const int64_t seekTo = m_seekTarget.exchange(-1);
    if (seekTo >= 0) {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk)
            ma_decoder_seek_to_pcm_frame(m_decoder, ma_uint64(seekTo));
        m_framePos.store(ma_uint64(seekTo));
    }

    // Ensure working buffers are large enough
    if (int(m_decodeBuf.size()) < frames * 2) {
        m_decodeBuf.resize(frames * 2);
        m_plugL.resize(frames);
        m_plugR.resize(frames);
    }

    ma_uint64 framesRead = 0;
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (!m_decoderOk) return false;
        ma_decoder_read_pcm_frames(m_decoder, m_decodeBuf.data(),
                                   ma_uint64(frames), &framesRead);
    }

    if (framesRead == 0) {
        // End of file
        if (m_loopsRemaining.load() > 1) {
            m_loopsRemaining.fetch_sub(1);
            const ma_uint64 startFrame = ma_uint64(m_trimStart * sampleRate);
            std::lock_guard<std::mutex> lock(m_decoderMtx);
            if (m_decoderOk) {
                ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
                m_framePos.store(startFrame);
            }
        } else {
            return false;  // done → engine will call onRenderFinished
        }
        return true;
    }

    const ma_uint64 curPos = ma_uint64(m_framePos.load());
    const ma_uint64 newPos = curPos + framesRead;
    m_framePos.store(newPos);

    // Trim end check
    if (m_trimEnd > 0.001) {
        const ma_uint64 trimEndFrame = ma_uint64(m_trimEnd * sampleRate);
        if (newPos >= trimEndFrame) {
            if (m_loopsRemaining.load() > 1) {
                m_loopsRemaining.fetch_sub(1);
                const ma_uint64 startFrame = ma_uint64(m_trimStart * sampleRate);
                std::lock_guard<std::mutex> lock(m_decoderMtx);
                if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
                m_framePos.store(startFrame);
                return true;
            }
            return false;
        }
    }

    // Compute volume envelope for this block
    const double durS  = m_fileDuration > 0.001 ? m_fileDuration : 1.0;
    const double posS  = double(curPos) / double(sampleRate > 0 ? sampleRate : 48000);
    const double normT = qBound(0.0, posS / durS, 1.0);

    double volEnv = 1.0;
    if (!m_volumePoints.isEmpty()) {
        volEnv = interpolateVolume(normT);
    } else {
        if (m_fadeIn > 0.01)
            volEnv *= qMin(1.0, posS / m_fadeIn);
        if (m_fadeOut > 0.01) {
            const double remaining = durS - posS;
            volEnv *= qMin(1.0, remaining / m_fadeOut);
        }
    }
    const float vol = float(m_targetVolume * m_playbackScale * volEnv);

    // Deinterleave decoded PCM → plugin chain → interleave to output
    const int n = int(framesRead);
    for (int i = 0; i < n; ++i) {
        m_plugL[i] = m_decodeBuf[i * 2]     * vol;
        m_plugR[i] = m_decodeBuf[i * 2 + 1] * vol;
    }

    // Apply channel routing
    if (m_channel == ChannelRoute::Left) {
        std::fill(m_plugR.begin(), m_plugR.begin() + n, 0.0f);
    } else if (m_channel == ChannelRoute::Right) {
        std::fill(m_plugL.begin(), m_plugL.begin() + n, 0.0f);
    }

    // Plugin chain (processes m_plugL/m_plugR in-place)
    float *ch[2] = { m_plugL.data(), m_plugR.data() };
    m_chain.process(ch, n);

    // Mix into output (stereo interleaved)
    for (int i = 0; i < n; ++i) {
        out[i * 2]     += m_plugL[i];
        out[i * 2 + 1] += m_plugR[i];
    }

    // Zero-fill if decoder returned fewer frames than requested (end of file)
    for (int i = n; i < frames; ++i) {
        out[i * 2] = out[i * 2 + 1] = 0.0f;
    }

    return true;
}

void AudioCue::onRenderFinished() {
    // Called from the audio thread — post to main thread via queued connection
    QMetaObject::invokeMethod(this, &AudioCue::handleStreamFinished,
                              Qt::QueuedConnection);
}

void AudioCue::handleStreamFinished() {
    // Main thread: clean up playing state and emit finished
    m_playing.store(false);
    m_loopsRemaining.store(1);
    // Seek back to start
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) {
            ma_decoder_seek_to_pcm_frame(m_decoder, 0);
            m_framePos.store(0);
        }
    }
    setState(State::Idle);
    emit finished();
}

// ── Volume automation ─────────────────────────────────────────────────────────

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

// ── JSON ──────────────────────────────────────────────────────────────────────

QJsonObject AudioCue::toJson() const {
    auto obj        = Cue::toJson();
    obj["cueType"]  = "audio";
    obj["filePath"] = m_filePath;
    obj["volume"]   = m_targetVolume;
    obj["fadeIn"]   = m_fadeIn;
    obj["fadeOut"]  = m_fadeOut;
    obj["trimStart"] = m_trimStart;
    obj["trimEnd"]   = m_trimEnd;
    obj["channel"]   = int(m_channel);
    obj["loopCount"] = m_loopCount;
    obj["plugins"]   = m_chain.toJson();

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
    m_targetVolume = o["volume"].toDouble(1.0);
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
    m_chain.fromJson(o["plugins"].toArray());
}
