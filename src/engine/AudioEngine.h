#pragma once
#include <vector>
#include <mutex>
#include <memory>

// AudioRenderer: implemented by AudioCue to fill audio buffers from the audio thread.
class AudioRenderer {
public:
    virtual ~AudioRenderer() = default;
    // Called from the audio thread. Mix up to `frames` stereo-interleaved samples
    // into `out` (add to existing content). Returns false when the stream is done.
    virtual bool renderAudio(float* out, int frames, int sampleRate) = 0;
    // Called from the audio thread when renderAudio returns false.
    // Must schedule main-thread cleanup (e.g. via QMetaObject::invokeMethod queued).
    virtual void onRenderFinished() = 0;
};

// Singleton that owns the miniaudio device and mixes all active AudioRenderers.
class AudioEngine {
public:
    static AudioEngine &instance();

    bool init();
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    int  sampleRate()    const { return m_sampleRate; }
    int  blockSize()     const { return m_blockSize;  }

    void addRenderer(AudioRenderer *r);
    void removeRenderer(AudioRenderer *r);

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

private:
    AudioEngine();
    ~AudioEngine();

    // pimpl for miniaudio types to avoid polluting all include paths
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_initialized = false;
    int  m_sampleRate  = 48000;
    int  m_blockSize   = 512;

    mutable std::mutex         m_mutex;
    std::vector<AudioRenderer*> m_renderers;

    static void maDeviceCallback(void* device, void* pOut,
                                 const void*, unsigned int frames);
};
