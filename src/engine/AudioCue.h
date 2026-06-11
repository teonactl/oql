#pragma once
#include "Cue.h"
#include "AudioEngine.h"
#include "PluginChain.h"
#include <QTimer>
#include <QVector>
#include <QPointF>
#include <atomic>
#include <mutex>
#include <vector>

struct AudioSlice {
    double posSec    = 0.0; // absolute seconds from file start
    int    loopCount = 1;   // 0=skip, 1=play once, N>1=loop N times
};

// Forward-declare miniaudio types (implementation in AudioCue.cpp which includes miniaudio.h)
struct ma_decoder;

class AudioCue : public Cue, private AudioRenderer {
    Q_OBJECT
public:
    enum class ChannelRoute { Both = 0, Left, Right };

    explicit AudioCue(QObject *parent = nullptr);
    ~AudioCue() override;

    Type    type()     const override { return Type::Audio; }
    QString typeName() const override { return "Audio"; }

    // ── Properties ────────────────────────────────────────────────────────────
    QString  filePath()        const { return m_filePath; }
    double   volume()          const { return m_targetVolume; }
    double   fadeInDuration()  const { return m_fadeIn; }
    double   fadeOutDuration() const { return m_fadeOut; }
    ChannelRoute channelRoute()       const { return m_channel; }
    QVector<QPointF> volumePoints()   const { return m_volumePoints; }
    double   trimStart() const { return m_trimStart; }
    double   trimEnd()   const { return m_trimEnd; }
    int      loopCount() const { return m_loopCount; }
    double   userRate()  const { return m_userRate; }
    QVector<AudioSlice> slices() const { return m_slices; }

    void setFilePath(const QString &path);
    void setVolume(double v);
    void setPlaybackVolume(double v) override;
    void setFadeInDuration(double s)    { m_fadeIn  = s; emit propertyChanged(); }
    void setFadeOutDuration(double s)   { m_fadeOut = s; emit propertyChanged(); }
    void setChannelRoute(ChannelRoute r){ m_channel = r; emit propertyChanged(); }
    void setVolumePoints(const QVector<QPointF> &pts){ m_volumePoints = pts; emit propertyChanged(); }
    void setTrimStart(double s)         { m_trimStart = qMax(0.0,s); emit propertyChanged(); }
    void setTrimEnd(double s)           { m_trimEnd   = qMax(0.0,s); emit propertyChanged(); }
    void setLoopCount(int n)            { m_loopCount = qMax(0,n);   emit propertyChanged(); }
    void setUserRate(double r)          { m_userRate = qBound(0.1, r, 4.0); emit propertyChanged(); }
    void setSlices(const QVector<AudioSlice> &s) { m_slices = s; emit propertyChanged(); }
    void setPlaybackRate(double r) override;

    // ── Plugin chain (main thread access only) ────────────────────────────────
    PluginChain       *pluginChain()       { return &m_chain; }
    const PluginChain *pluginChain() const { return &m_chain; }

    // Snapshot API for EffectCue / ResetEffectCue (main thread only)
    void savePluginSnapshot();
    bool restorePluginSnapshot();
    bool hasPluginSnapshot() const { return m_hasPluginSnapshot; }
    void applyPluginChain(const QJsonArray &json);

    // ── Playback ──────────────────────────────────────────────────────────────
    void   go()              override;
    void   stop()            override;
    void   pause()           override;
    double duration() const  override;
    double position() const  override;

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o)  override;

private:
    // AudioRenderer — called from audio thread
    bool renderAudio(float* out, int frames, int sampleRate) override;
    void onRenderFinished() override;

    // Main-thread slot posted from audio thread
    Q_INVOKABLE void handleStreamFinished();

    double interpolateVolume(double t) const;

    // ── Decoder (accessed only inside renderAudio with m_decoderMtx) ─────────
    std::mutex        m_decoderMtx;
    ma_decoder       *m_decoder    = nullptr;
    bool              m_decoderOk  = false;
    uint64_t          m_totalFrames = 0;
    double            m_fileDuration = 0.0; // seconds, cached

    // ── Render state (atomics readable from both threads) ─────────────────────
    std::atomic<bool>     m_playing    {false};
    std::atomic<bool>     m_paused     {false};
    std::atomic<int64_t>  m_seekTarget {-1};     // -1 = no pending seek
    std::atomic<uint64_t> m_framePos   {0};

    // Set once in go(), read in renderAudio() — safe because audio thread
    // starts only after go() returns.
    double   m_targetVolume   = 1.0;
    double   m_playbackScale  = 1.0;   // from FadeCue::setPlaybackVolume
    double   m_playbackRate   = 1.0;   // from SpeedCue::setPlaybackRate
    double   m_userRate       = 1.0;   // user-set persistent rate
    int      m_currentDecoderSR = 48000; // decoder output SR (engineSR / rate), set in go()
    double   m_fadeIn         = 3.0;
    double   m_fadeOut       = 3.0;
    double   m_trimStart     = 0.0;
    double   m_trimEnd       = 0.0;
    int      m_loopCount     = 1;     // 0 = infinite, N = play N times

    // User-defined slices (main thread)
    QVector<AudioSlice> m_slices;

    // Loop state — written in go(), mutated in renderAudio()
    std::atomic<int> m_loopsRemaining{1};

    // Slice playback state — snapshot in go(), read/written only in renderAudio()
    struct SliceRange { uint64_t startFrame; uint64_t endFrame; int loopCount; };
    std::vector<SliceRange> m_sliceRanges;  // set in go() before addRenderer
    int m_audioSliceIdx   = 0;              // audio thread only
    int m_audioSliceLoops = 1;             // audio thread only

    // Per-render volume (computed in renderAudio() from fade envelope)
    float    m_renderVol     = 1.0f;

    ChannelRoute     m_channel = ChannelRoute::Both;
    QVector<QPointF> m_volumePoints;
    QString          m_filePath;

    PluginChain      m_chain;
    mutable std::mutex m_chainMtx;     // guards m_chain between main and audio thread
    int              m_chainSR    = 0; // sample rate used in last prepare()
    int              m_chainBlock = 0; // block size used in last prepare()
    QJsonArray       m_chainSnapshot;
    bool             m_hasPluginSnapshot = false;

    // Temp PCM buffers used inside renderAudio (preallocated in go())
    std::vector<float> m_decodeBuf;  // interleaved stereo from decoder
    std::vector<float> m_plugL, m_plugR; // deinterleaved for plugin chain
};
