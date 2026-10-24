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

// Singleton that owns the "main" miniaudio device (the one configured in
// Impostazioni → Hardware) plus, on demand, one extra device per additional
// physical output that a cue asks to route to (see addRenderer). Every open
// device continuously mixes a near-inaudible periodic ping into its stream
// (same technique as BlueCue) so Bluetooth speakers don't fall asleep/
// disconnect during silence between cues.
class AudioEngine {
public:
    // Una uscita fisica aperta (device + i suoi renderer + stato keepalive).
    // Definita in .cpp; pubblica solo per essere nominabile dalle funzioni
    // libere che implementano la callback audio (vedi audioDeviceCallback).
    struct Output;

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

    // deviceName: quale uscita fisica usare per questo renderer ("" = uscita
    // principale, quella configurata in Impostazioni → Hardware). Se il
    // dispositivo non è ancora aperto, viene avviato al volo (stesso sample
    // rate/formato dell'uscita principale, tramite il resampler di miniaudio).
    void addRenderer(AudioRenderer *r, const std::string &deviceName = {});
    // Cerca il renderer su tutte le uscite aperte e lo rimuove: il chiamante
    // non deve tenere traccia di quale uscita stesse usando.
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

    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};
    std::atomic<int>   m_dropoutCount{0};

    // Callback del device miniaudio per una singola Output — legge/scrive i
    // contatori di peak/dropout dell'engine solo per l'uscita principale.
    // friend (non membro) perché il puntatore alla funzione va passato a
    // miniaudio senza portarsi dietro i tipi miniaudio in questo header.
    friend void audioDeviceCallback(Output *out_, void *pOutput, unsigned int frameCount);
};
