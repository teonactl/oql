#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <memory>
#include <string>

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

struct AudioDeviceInfo {
    std::string name;
    bool        isDefault = false;
};

// Singleton that owns the miniaudio device and mixes all active AudioRenderers.
class AudioEngine {
public:
    static AudioEngine &instance();

    bool init(const std::string &deviceName = {});
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    int  sampleRate()    const { return m_sampleRate; }
    int  blockSize()     const { return m_blockSize;  }

    // Device enumeration and switching (call from main thread only)
    std::vector<AudioDeviceInfo> enumerateDevices() const;
    bool reinitWithDevice(const std::string &deviceName);  // empty = default device
    std::string currentDeviceName() const { return m_currentDeviceName; }

    void addRenderer(AudioRenderer *r);
    void removeRenderer(AudioRenderer *r);

    float peakL() const { return m_peakL.load(std::memory_order_relaxed); }
    float peakR() const { return m_peakR.load(std::memory_order_relaxed); }

    // Counts how many audio callbacks returned early (try_to_lock failed = dropout)
    int  dropoutCount() const { return m_dropoutCount.load(std::memory_order_relaxed); }
    void resetDropoutCount()  { m_dropoutCount.store(0, std::memory_order_relaxed); }

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

private:
    AudioEngine();
    ~AudioEngine();

    // pimpl for miniaudio types to avoid polluting all include paths
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool        m_initialized     = false;
    int         m_sampleRate      = 48000;
    int         m_blockSize       = 512;
    std::string m_currentDeviceName;

    mutable std::mutex         m_mutex;
    std::vector<AudioRenderer*> m_renderers;

    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    std::atomic<int>   m_dropoutCount{0};

    static void maDeviceCallback(void* device, void* pOut,
                                 const void*, unsigned int frames);
};
