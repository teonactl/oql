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
    m_chain   = std::make_shared<PluginChain>();
    m_activeChain.store(m_chain.get(), std::memory_order_release);
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

// ── Plugin chain snapshot (for EffectCue / ResetEffectCue) ───────────────────

void AudioCue::savePluginSnapshot() {
    m_chainSnapshot     = m_chain->toJson();
    m_hasPluginSnapshot = true;
}

bool AudioCue::restorePluginSnapshot() {
    if (!m_hasPluginSnapshot) return false;
    int sr = m_chainSR.load(), block = m_chainBlock.load();
    auto newChain = std::make_shared<PluginChain>();
    newChain->fromJson(m_chainSnapshot);
    if (m_playing.load() && sr > 0) {
        newChain->prepare(sr, block);
        std::vector<float> silL(block, 0.0f), silR(block, 0.0f);
        float *silCh[2] = { silL.data(), silR.data() };
        newChain->process(silCh, block);
    }

    auto oldChain = m_chain;
    m_chain = newChain;
    // Atomic swap at next callback boundary — old chain kept alive for 500ms
    // (callbacks run every ~10ms, so this is well past any in-flight process())
    m_activeChain.store(newChain.get(), std::memory_order_release);
    QTimer::singleShot(500, [kept = std::move(oldChain)](){});

    emit displayChanged();
    emit pluginSnapshotRestored();
    return true;
}

void AudioCue::applyPluginChain(const QJsonArray &json) {
    int sr = m_chainSR.load(), block = m_chainBlock.load();
    auto newChain = std::make_shared<PluginChain>();
    newChain->fromJson(json);
    if (m_playing.load() && sr > 0) {
        newChain->prepare(sr, block);
        // Pre-warm: run one silent buffer through the new chain on the main thread.
        // This triggers any internal allocations and stabilises plugin state
        // before the audio thread ever sees this chain — no malloc in RT path.
        std::vector<float> silL(block, 0.0f), silR(block, 0.0f);
        float *silCh[2] = { silL.data(), silR.data() };
        newChain->process(silCh, block);
    }

    auto oldChain = m_chain;
    m_chain = newChain;
    m_activeChain.store(newChain.get(), std::memory_order_release);
    QTimer::singleShot(500, [kept = std::move(oldChain)](){});

    emit displayChanged();
}

// ── Volume helpers ────────────────────────────────────────────────────────────

void AudioCue::setVolume(double v) {
    m_targetVolume = qBound(0.0, v, 1.0);
    emit propertyChanged();
}

void AudioCue::setPlaybackVolume(double v) {
    const double clamped = qBound(0.0, v, 1.0);
    m_playbackScale = (m_targetVolume > 0.001)
        ? clamped / m_targetVolume
        : (clamped > 0.001 ? 1.0 : 0.0);
}

void AudioCue::setUserRate(double r) {
    const double clamped = qBound(0.1, r, 4.0);
    if (qFuzzyCompare(clamped, m_userRate)) return;
    m_userRate = clamped;
    emit propertyChanged();
    if (!m_playing.load()) return;

    // Apply rate change in real-time (same pattern as setPlaybackRate)
    const double posSeconds = double(m_framePos.load())
                              / double(m_currentDecoderSR > 0 ? m_currentDecoderSR : 48000);
    m_playing.store(false);
    AudioEngine::instance().removeRenderer(this);

    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) { ma_decoder_uninit(m_decoder); m_decoderOk = false; }

        const int engineSR = AudioEngine::instance().sampleRate();
        const double effectiveRate = qBound(0.1, m_userRate * m_playbackRate, 4.0);
#ifdef HAVE_SOUNDTOUCH
        const uint32_t outSR = m_pitchPreserve
            ? uint32_t(engineSR) : uint32_t(double(engineSR) / effectiveRate);
#else
        const uint32_t outSR = uint32_t(double(engineSR) / effectiveRate);
#endif
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, outSR);
        if (ma_decoder_init_file(m_filePath.toUtf8().constData(), &cfg, m_decoder) != MA_SUCCESS)
            return;
        m_decoderOk = true;
        m_currentDecoderSR = int(outSR);

        const ma_uint64 seekFrame = ma_uint64(posSeconds * double(outSR));
        ma_decoder_seek_to_pcm_frame(m_decoder, seekFrame);
        m_framePos.store(seekFrame);

        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(m_decoder, &total);
        m_totalFrames = total;
    }

#ifdef HAVE_SOUNDTOUCH
    if (m_pitchPreserve) {
        const int engineSR = AudioEngine::instance().sampleRate();
        const double effectiveRate = qBound(0.1, m_userRate * m_playbackRate, 4.0);
        m_soundTouch.clear();
        m_soundTouch.setChannels(2);
        m_soundTouch.setSampleRate(uint(engineSR));
        m_soundTouch.setTempo(effectiveRate);
        m_soundTouch.setPitch(1.0);
        m_stAccum.clear();
        m_stFlushDone = false;
    }
#endif

    m_playing.store(true);
    AudioEngine::instance().addRenderer(this);
}

void AudioCue::setPitchPreserve(bool p) {
    if (m_pitchPreserve == p) return;

    if (!m_playing.load()) {
        m_pitchPreserve = p;
        emit propertyChanged();
        return;
    }

    // Stop audio thread BEFORE changing flag — prevents renderAudio from seeing
    // m_pitchPreserve=true while SoundTouch is uninitialised (race → crash)
    const double pos = position();
    m_playing.store(false);
    AudioEngine::instance().removeRenderer(this);

    // Audio thread is now stopped: safe to modify all state
    m_pitchPreserve = p;

    const int engineSR = AudioEngine::instance().sampleRate();
    const double effectiveRate = qBound(0.1, m_userRate * m_playbackRate, 4.0);

    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) { ma_decoder_uninit(m_decoder); m_decoderOk = false; }
#ifdef HAVE_SOUNDTOUCH
        const uint32_t outSR = m_pitchPreserve
            ? uint32_t(engineSR) : uint32_t(double(engineSR) / effectiveRate);
#else
        const uint32_t outSR = uint32_t(double(engineSR) / effectiveRate);
#endif
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, outSR);
        if (ma_decoder_init_file(m_filePath.toUtf8().constData(), &cfg, m_decoder) != MA_SUCCESS)
            return;
        m_decoderOk = true;
        m_currentDecoderSR = int(outSR);
        const ma_uint64 seekFrame = ma_uint64(pos * double(outSR));
        ma_decoder_seek_to_pcm_frame(m_decoder, seekFrame);
        m_framePos.store(seekFrame);
    }

#ifdef HAVE_SOUNDTOUCH
    if (m_pitchPreserve) {
        m_soundTouch.clear();
        m_soundTouch.setChannels(2);
        m_soundTouch.setSampleRate(uint(engineSR));
        m_soundTouch.setTempo(effectiveRate);
        m_soundTouch.setPitch(1.0);
        m_stAccum.clear();
        m_stFlushDone = false;
    }
#endif

    emit propertyChanged();
    m_playing.store(true);
    AudioEngine::instance().addRenderer(this);
}

void AudioCue::setPlaybackRate(double r) {
    m_playbackRate = qBound(0.1, r, 4.0);
    if (!m_playing.load()) return;

    // Cue is already playing: preserve position, reinit decoder at new rate
    const double posSeconds = double(m_framePos.load())
                              / double(m_currentDecoderSR > 0 ? m_currentDecoderSR : 48000);

    m_playing.store(false);
    AudioEngine::instance().removeRenderer(this);

    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) { ma_decoder_uninit(m_decoder); m_decoderOk = false; }

        const int engineSR = AudioEngine::instance().sampleRate();
        const double effectiveRate = qBound(0.1, m_userRate * m_playbackRate, 4.0);
        const uint32_t outSR = uint32_t(double(engineSR) / effectiveRate);

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, outSR);
        if (ma_decoder_init_file(m_filePath.toUtf8().constData(), &cfg, m_decoder) != MA_SUCCESS)
            return;
        m_decoderOk = true;
        m_currentDecoderSR = int(outSR);

        const ma_uint64 seekFrame = ma_uint64(posSeconds * outSR);
        ma_decoder_seek_to_pcm_frame(m_decoder, seekFrame);
        m_framePos.store(seekFrame);

        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(m_decoder, &total);
        m_totalFrames = total;
    }

    m_playing.store(true);
    AudioEngine::instance().addRenderer(this);
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

    if (m_filePath.isEmpty()) return;

    // Reset transient overrides left by FadeCue / SpeedCue (m_userRate is persistent)
    m_playbackScale = 1.0;
    m_playbackRate  = 1.0;

    // Reinitialize decoder: with pitch preserve → original SR; without → SR/rate
    {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (m_decoderOk) { ma_decoder_uninit(m_decoder); m_decoderOk = false; }

        const int engineSR = AudioEngine::instance().sampleRate();
        const double effectiveRate = qBound(0.1, m_userRate, 4.0);
#ifdef HAVE_SOUNDTOUCH
        const uint32_t outSR = m_pitchPreserve
            ? uint32_t(engineSR)
            : uint32_t(double(engineSR) / effectiveRate);
#else
        const uint32_t outSR = uint32_t(double(engineSR) / effectiveRate);
#endif

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, outSR);
        if (ma_decoder_init_file(m_filePath.toUtf8().constData(), &cfg, m_decoder) != MA_SUCCESS)
            return;
        m_decoderOk = true;
        m_currentDecoderSR = int(outSR);

        const ma_uint64 startFrame = ma_uint64(m_trimStart * outSR);
        ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
        m_framePos.store(startFrame);

        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(m_decoder, &total);
        m_totalFrames = total;
    }

#ifdef HAVE_SOUNDTOUCH
    if (m_pitchPreserve) {
        const int engineSR = AudioEngine::instance().sampleRate();
        const double effectiveRate = qBound(0.1, m_userRate, 4.0);
        m_soundTouch.clear();
        m_soundTouch.setChannels(2);
        m_soundTouch.setSampleRate(uint(engineSR));
        m_soundTouch.setTempo(effectiveRate);
        m_soundTouch.setPitch(1.0);
        m_stAccum.clear();
        m_stFlushDone = false;
    }
#endif

    m_loopsRemaining.store(m_loopCount > 0 ? m_loopCount : INT_MAX);

    // Build slice ranges in decoder-frame space (read in renderAudio, set before addRenderer)
    m_sliceRanges.clear();
    if (!m_slices.isEmpty()) {
        const double startSec = m_trimStart;
        const double endSec   = (m_trimEnd > 0.001) ? m_trimEnd : m_fileDuration;
        const int    sr       = m_currentDecoderSR;

        // Sort slice positions
        QVector<AudioSlice> sorted = m_slices;
        std::sort(sorted.begin(), sorted.end(),
                  [](const AudioSlice &a, const AudioSlice &b){ return a.posSec < b.posSec; });

        // Build segment boundary list: startSec, then each slice.posSec inside range, then endSec
        QVector<double> bounds;
        bounds.append(startSec);
        for (const auto &s : sorted) {
            if (s.posSec > startSec + 0.001 && s.posSec < endSec - 0.001)
                bounds.append(s.posSec);
        }
        bounds.append(endSec);

        // Create a SliceRange per segment; loop count comes from the matching AudioSlice
        for (int i = 0; i < bounds.size() - 1; ++i) {
            SliceRange r;
            r.startFrame = uint64_t(bounds[i] * sr);
            r.endFrame   = uint64_t(bounds[i+1] * sr);
            // Segment i starts at bounds[i]; the AudioSlice with the closest posSec owns it
            r.loopCount = 1;
            if (i < sorted.size())
                r.loopCount = sorted[i].loopCount;
            m_sliceRanges.push_back(r);
        }
    }

    // Find first non-skip slice
    m_audioSliceIdx = 0;
    if (!m_sliceRanges.empty()) {
        while (m_audioSliceIdx < int(m_sliceRanges.size()) &&
               m_sliceRanges[m_audioSliceIdx].loopCount == 0)
            m_audioSliceIdx++;
        m_audioSliceLoops = (m_audioSliceIdx < int(m_sliceRanges.size()))
                            ? m_sliceRanges[m_audioSliceIdx].loopCount : 1;
        // Seek to first non-skip slice
        if (m_audioSliceIdx < int(m_sliceRanges.size())) {
            std::lock_guard<std::mutex> lock(m_decoderMtx);
            if (m_decoderOk) {
                ma_decoder_seek_to_pcm_frame(m_decoder, m_sliceRanges[m_audioSliceIdx].startFrame);
                m_framePos.store(m_sliceRanges[m_audioSliceIdx].startFrame);
            }
        }
    } else {
        m_audioSliceLoops = m_loopCount > 0 ? m_loopCount : INT_MAX;
    }
    m_renderVol    = (m_fadeIn > 0.01) ? 0.0f : float(m_targetVolume);
    m_paused.store(false);
    m_seekTarget.store(-1);

    // Preallocate working buffers
    const int block = AudioEngine::instance().blockSize();
    m_decodeBuf.resize(block * 2);
    m_plugL.resize(block);
    m_plugR.resize(block);

    // Store SR/block as atomics so applyPluginChain() can read them lock-free.
    // addRenderer() is called below, so no audio thread is running yet.
    m_chainSR.store(AudioEngine::instance().sampleRate());
    m_chainBlock.store(block);
    m_chain->prepare(m_chainSR.load(), m_chainBlock.load());
    m_activeChain.store(m_chain.get(), std::memory_order_release);

    // Save snapshot BEFORE audio starts — prevents main thread from calling
    // effGetChunk / toJson() on plugins while the audio thread is in process().
    if (!m_hasPluginSnapshot)
        savePluginSnapshot();

    m_playing.store(true);
    AudioEngine::instance().addRenderer(this);
    setState(State::Playing);
}

void AudioCue::stop() {
    if (!m_playing.load() && m_state == State::Idle) return;
    m_playing.store(false);
    m_paused.store(false);
    m_playbackScale = 1.0;  // reset fade scale
    m_playbackRate  = 1.0;  // reset speed change
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

#ifdef HAVE_SOUNDTOUCH
    // ── Pitch-preserving path via SoundTouch ─────────────────────────────────
    if (m_pitchPreserve) {
        const double effectiveRate = qBound(0.1, m_userRate * m_playbackRate, 4.0);
        const bool   sliceMode     = !m_sliceRanges.empty();

        // Feed SoundTouch until accumulator has enough output or we reach EOF
        while (int(m_stAccum.size()) < frames * 2 && !m_stFlushDone) {
            // Batch size: more than needed for rate > 1 so ST can produce frames output
            const int need  = frames - int(m_stAccum.size()) / 2;
            const int batch = std::max(int(std::ceil(double(need) * effectiveRate)) + 256, frames);
            if (int(m_stInBuf.size()) < batch * 2) m_stInBuf.resize(batch * 2);

            // Apply slice limit to batch
            int batchToRead = batch;
            if (sliceMode) {
                if (m_audioSliceIdx >= int(m_sliceRanges.size())) {
                    m_soundTouch.flush(); m_stFlushDone = true; break;
                }
                const uint64_t pos      = m_framePos.load();
                const uint64_t sliceEnd = m_sliceRanges[m_audioSliceIdx].endFrame;
                batchToRead = (pos < sliceEnd)
                    ? int(std::min(uint64_t(batch), sliceEnd - pos)) : 0;
            }

            ma_uint64 rawRead = 0;
            if (batchToRead > 0) {
                std::lock_guard<std::mutex> lock(m_decoderMtx);
                if (!m_decoderOk) return false;
                ma_decoder_read_pcm_frames(m_decoder, m_stInBuf.data(), batchToRead, &rawRead);
            }

            const uint64_t newPos = m_framePos.load() + rawRead;
            m_framePos.store(newPos);

            if (rawRead > 0)
                m_soundTouch.putSamples(m_stInBuf.data(), uint(rawRead));

            // Drain ST into accumulator
            for (;;) {
                const size_t prevSz = m_stAccum.size();
                m_stAccum.resize(prevSz + size_t(batch) * 2);
                const uint got = m_soundTouch.receiveSamples(
                    m_stAccum.data() + prevSz, uint(batch));
                m_stAccum.resize(prevSz + got * 2);
                if (got == 0) break;
            }

            // Detect end-of-slice / EOF / trim-end
            bool atEnd = (rawRead == 0 || batchToRead == 0);
            if (!atEnd && !sliceMode && m_trimEnd > 0.001) {
                const uint64_t te = uint64_t(m_trimEnd * m_currentDecoderSR);
                if (newPos >= te) atEnd = true;
            }
            if (!atEnd && sliceMode) {
                const uint64_t se = m_sliceRanges[m_audioSliceIdx].endFrame;
                if (newPos >= se) atEnd = true;
            }

            if (atEnd) {
                if (sliceMode) {
                    m_audioSliceLoops--;
                    if (m_audioSliceLoops > 0) {
                        const uint64_t s = m_sliceRanges[m_audioSliceIdx].startFrame;
                        std::lock_guard<std::mutex> lock(m_decoderMtx);
                        if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, s);
                        m_framePos.store(s);
                        m_soundTouch.clear();
                    } else {
                        m_audioSliceIdx++;
                        while (m_audioSliceIdx < int(m_sliceRanges.size()) &&
                               m_sliceRanges[m_audioSliceIdx].loopCount == 0)
                            m_audioSliceIdx++;
                        if (m_audioSliceIdx >= int(m_sliceRanges.size())) {
                            m_soundTouch.flush(); m_stFlushDone = true; break;
                        }
                        const uint64_t s = m_sliceRanges[m_audioSliceIdx].startFrame;
                        m_audioSliceLoops = m_sliceRanges[m_audioSliceIdx].loopCount;
                        std::lock_guard<std::mutex> lock(m_decoderMtx);
                        if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, s);
                        m_framePos.store(s);
                        m_soundTouch.clear();
                    }
                } else {
                    if (m_loopsRemaining.load() > 1) {
                        m_loopsRemaining.fetch_sub(1);
                        const ma_uint64 startFrame = ma_uint64(m_trimStart * m_currentDecoderSR);
                        std::lock_guard<std::mutex> lock(m_decoderMtx);
                        if (m_decoderOk) {
                            ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
                            m_framePos.store(startFrame);
                        }
                        m_soundTouch.clear();
                    } else {
                        m_soundTouch.flush(); m_stFlushDone = true; break;
                    }
                }
            }
        }

        // After flush: drain any remaining ST output
        if (m_stFlushDone) {
            for (;;) {
                const size_t prevSz = m_stAccum.size();
                m_stAccum.resize(prevSz + size_t(frames) * 2);
                const uint got = m_soundTouch.receiveSamples(
                    m_stAccum.data() + prevSz, uint(frames));
                m_stAccum.resize(prevSz + got * 2);
                if (got == 0) break;
            }
        }

        const int available = int(m_stAccum.size()) / 2;
        if (available == 0) return false;

        const int n = std::min(available, frames);
        if (int(m_plugL.size()) < n) { m_plugL.resize(n); m_plugR.resize(n); }

        // Volume envelope (posS = file position in seconds)
        const uint64_t curPos = m_framePos.load();
        const double durS  = m_fileDuration > 0.001 ? m_fileDuration : 1.0;
        const double posS  = double(curPos) / double(m_currentDecoderSR > 0 ? m_currentDecoderSR : 48000);
        const double normT = qBound(0.0, posS / durS, 1.0);

        double volEnv = 1.0;
        if (!m_volumePoints.isEmpty()) {
            volEnv = interpolateVolume(normT);
        } else {
            if (m_fadeIn  > 0.01) volEnv *= qMin(1.0, posS / m_fadeIn);
            if (m_fadeOut > 0.01) {
                const double remaining = durS - posS;
                volEnv *= qMin(1.0, remaining / m_fadeOut);
            }
        }
        const float vol = float(m_targetVolume * m_playbackScale * volEnv);

        for (int i = 0; i < n; ++i) {
            m_plugL[i] = m_stAccum[i * 2]     * vol;
            m_plugR[i] = m_stAccum[i * 2 + 1] * vol;
        }
        m_stAccum.erase(m_stAccum.begin(), m_stAccum.begin() + n * 2);

        if (m_channel == ChannelRoute::Left)
            std::fill(m_plugR.begin(), m_plugR.begin() + n, 0.0f);
        else if (m_channel == ChannelRoute::Right)
            std::fill(m_plugL.begin(), m_plugL.begin() + n, 0.0f);

        float *ch[2] = { m_plugL.data(), m_plugR.data() };
        m_activeChain.load(std::memory_order_acquire)->process(ch, n);

        for (int i = 0; i < n; ++i) {
            out[i * 2]     += m_plugL[i];
            out[i * 2 + 1] += m_plugR[i];
        }
        for (int i = n; i < frames; ++i) out[i * 2] = out[i * 2 + 1] = 0.0f;

        return true;
    }
#endif

    // Limit read to current slice boundary (if slices active)
    const bool sliceMode = !m_sliceRanges.empty();
    int framesToRead = frames;
    if (sliceMode) {
        if (m_audioSliceIdx >= int(m_sliceRanges.size())) return false;
        const uint64_t pos     = m_framePos.load();
        const uint64_t sliceEnd = m_sliceRanges[m_audioSliceIdx].endFrame;
        framesToRead = (pos < sliceEnd)
            ? int(std::min(uint64_t(frames), sliceEnd - pos))
            : 0;
    }

    ma_uint64 framesRead = 0;
    if (framesToRead > 0) {
        std::lock_guard<std::mutex> lock(m_decoderMtx);
        if (!m_decoderOk) return false;
        ma_decoder_read_pcm_frames(m_decoder, m_decodeBuf.data(),
                                   ma_uint64(framesToRead), &framesRead);
    }

    const ma_uint64 curPos = ma_uint64(m_framePos.load());
    const ma_uint64 newPos = curPos + framesRead;
    m_framePos.store(newPos);

    if (sliceMode) {
        // Slice boundary or EOF within slice
        const uint64_t sliceEnd = m_sliceRanges[m_audioSliceIdx].endFrame;
        if (framesRead == 0 || newPos >= sliceEnd) {
            m_audioSliceLoops--;
            if (m_audioSliceLoops > 0) {
                // Loop back to start of this slice
                const uint64_t s = m_sliceRanges[m_audioSliceIdx].startFrame;
                std::lock_guard<std::mutex> lock(m_decoderMtx);
                if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, s);
                m_framePos.store(s);
            } else {
                // Advance to next non-skip slice
                m_audioSliceIdx++;
                while (m_audioSliceIdx < int(m_sliceRanges.size()) &&
                       m_sliceRanges[m_audioSliceIdx].loopCount == 0)
                    m_audioSliceIdx++;
                if (m_audioSliceIdx >= int(m_sliceRanges.size()))
                    return false;  // all slices done
                const uint64_t s = m_sliceRanges[m_audioSliceIdx].startFrame;
                m_audioSliceLoops = m_sliceRanges[m_audioSliceIdx].loopCount;
                std::lock_guard<std::mutex> lock(m_decoderMtx);
                if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, s);
                m_framePos.store(s);
            }
        }
    } else {
        // Original no-slices EOF / trim / loop handling
        if (framesRead == 0) {
            if (m_loopsRemaining.load() > 1) {
                m_loopsRemaining.fetch_sub(1);
                const ma_uint64 startFrame = ma_uint64(m_trimStart * m_currentDecoderSR);
                std::lock_guard<std::mutex> lock(m_decoderMtx);
                if (m_decoderOk) {
                    ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
                    m_framePos.store(startFrame);
                }
                return true;
            }
            return false;
        }

        if (m_trimEnd > 0.001) {
            const ma_uint64 trimEndFrame = ma_uint64(m_trimEnd * m_currentDecoderSR);
            if (newPos >= trimEndFrame) {
                if (m_loopsRemaining.load() > 1) {
                    m_loopsRemaining.fetch_sub(1);
                    const ma_uint64 startFrame = ma_uint64(m_trimStart * m_currentDecoderSR);
                    std::lock_guard<std::mutex> lock(m_decoderMtx);
                    if (m_decoderOk) ma_decoder_seek_to_pcm_frame(m_decoder, startFrame);
                    m_framePos.store(startFrame);
                    return true;
                }
                return false;
            }
        }
    }

    // Compute volume envelope for this block
    // posS is in real seconds: decoder frames / decoderSR = real elapsed time
    const double durS  = m_fileDuration > 0.001 ? m_fileDuration : 1.0;
    const double posS  = double(curPos) / double(m_currentDecoderSR > 0 ? m_currentDecoderSR : 48000);
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

    // Plugin chain — lock-free atomic load.
    float *ch[2] = { m_plugL.data(), m_plugR.data() };
    m_activeChain.load(std::memory_order_acquire)->process(ch, n);

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
    m_playbackScale = 1.0;
    m_playbackRate  = 1.0;
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
    obj["userRate"]      = m_userRate;
    obj["pitchPreserve"] = m_pitchPreserve;
    obj["plugins"]   = m_chain->toJson();

    QJsonArray pts;
    for (const auto &p : m_volumePoints)
        pts.append(QJsonArray{p.x(), p.y()});
    obj["volumePoints"] = pts;

    QJsonArray slicesArr;
    for (const auto &s : m_slices)
        slicesArr.append(QJsonObject{{"pos", s.posSec}, {"loop", s.loopCount}});
    obj["slices"] = slicesArr;
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
    m_loopCount     = o["loopCount"].toInt(1);
    m_userRate      = o["userRate"].toDouble(1.0);
    m_pitchPreserve = o["pitchPreserve"].toBool(false);

    m_slices.clear();
    for (const auto &v : o["slices"].toArray()) {
        const auto obj2 = v.toObject();
        AudioSlice s;
        s.posSec    = obj2["pos"].toDouble();
        s.loopCount = obj2["loop"].toInt(1);
        m_slices.append(s);
    }

    m_volumePoints.clear();
    for (const auto &v : o["volumePoints"].toArray()) {
        const auto arr = v.toArray();
        if (arr.size() == 2)
            m_volumePoints.append(QPointF(arr[0].toDouble(), arr[1].toDouble()));
    }
    m_chain->fromJson(o["plugins"].toArray());
}
