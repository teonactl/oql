#include "AudioEngine.h"
#include "miniaudio.h"
#include <algorithm>
#include <vector>

// ── pimpl ─────────────────────────────────────────────────────────────────────

struct AudioEngine::Impl {
    ma_device  device{};
    ma_context context{};
    std::vector<float> mixBuf;
};

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

// ── Singleton ─────────────────────────────────────────────────────────────────

AudioEngine &AudioEngine::instance() {
    static AudioEngine inst;
    return inst;
}

// ── Device callback ───────────────────────────────────────────────────────────

void AudioEngine::maDeviceCallback(void* pUserData, void* pOutput,
                                   const void*, unsigned int frameCount)
{
    auto *engine = static_cast<AudioEngine *>(pUserData);
    float *out   = static_cast<float *>(pOutput);
    const int n  = int(frameCount) * 2;  // stereo interleaved
    std::fill(out, out + n, 0.0f);

    // Try to acquire mutex without blocking the audio thread.
    // If the main thread holds it (e.g. during add/remove), skip this block.
    std::unique_lock<std::mutex> lock(engine->m_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    // Ensure mix buffer is large enough
    auto &mb = engine->m_impl->mixBuf;
    if (int(mb.size()) < n) mb.resize(n);

    std::vector<AudioRenderer *> toFinish;

    for (auto *r : engine->m_renderers) {
        std::fill(mb.begin(), mb.begin() + n, 0.0f);
        if (!r->renderAudio(mb.data(), int(frameCount), engine->m_sampleRate)) {
            toFinish.push_back(r);
        } else {
            for (int i = 0; i < n; ++i)
                out[i] += mb[i];
        }
    }

    // Remove finished renderers while still holding the lock
    for (auto *r : toFinish) {
        auto it = std::find(engine->m_renderers.begin(), engine->m_renderers.end(), r);
        if (it != engine->m_renderers.end())
            engine->m_renderers.erase(it);
    }
    lock.unlock();

    // Notify finished renderers AFTER releasing the lock
    for (auto *r : toFinish)
        r->onRenderFinished();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool AudioEngine::init() {
    if (m_initialized) return true;
    m_impl = std::make_unique<Impl>();

    if (ma_context_init(nullptr, 0, nullptr, &m_impl->context) != MA_SUCCESS)
        return false;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format  = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate       = 0;  // let device pick best rate
    cfg.dataCallback     = [](ma_device* d, void* out, const void* in, ma_uint32 frames) {
        maDeviceCallback(d->pUserData, out, in, frames);
    };
    cfg.pUserData        = this;

    if (ma_device_init(&m_impl->context, &cfg, &m_impl->device) != MA_SUCCESS) {
        ma_context_uninit(&m_impl->context);
        return false;
    }
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        ma_device_uninit(&m_impl->device);
        ma_context_uninit(&m_impl->context);
        return false;
    }

    m_sampleRate  = int(m_impl->device.sampleRate);
    m_blockSize   = 512;
    m_initialized = true;
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;
    ma_device_stop(&m_impl->device);
    ma_device_uninit(&m_impl->device);
    ma_context_uninit(&m_impl->context);
    m_initialized = false;
}

void AudioEngine::addRenderer(AudioRenderer *r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (std::find(m_renderers.begin(), m_renderers.end(), r) == m_renderers.end())
        m_renderers.push_back(r);
}

void AudioEngine::removeRenderer(AudioRenderer *r) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_renderers.begin(), m_renderers.end(), r);
    if (it != m_renderers.end())
        m_renderers.erase(it);
}
