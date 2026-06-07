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
    void setPlaybackRate(double r) override;

    // ── Plugin chain (main thread access only) ────────────────────────────────
    PluginChain       *pluginChain()       { return &m_chain; }
    const PluginChain *pluginChain() const { return &m_chain; }

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
    double   m_targetVolume  = 1.0;
    double   m_playbackScale = 1.0;   // from FadeCue::setPlaybackVolume
    double   m_fadeIn        = 3.0;
    double   m_fadeOut       = 3.0;
    double   m_trimStart     = 0.0;
    double   m_trimEnd       = 0.0;
    int      m_loopCount     = 1;     // 0 = infinite, N = play N times

    // Loop state — written in go(), mutated in renderAudio()
    std::atomic<int> m_loopsRemaining{1};

    // Per-render volume (computed in renderAudio() from fade envelope)
    float    m_renderVol     = 1.0f;

    ChannelRoute     m_channel = ChannelRoute::Both;
    QVector<QPointF> m_volumePoints;
    QString          m_filePath;

    PluginChain m_chain;

    // Temp PCM buffers used inside renderAudio (preallocated in go())
    std::vector<float> m_decodeBuf;  // interleaved stereo from decoder
    std::vector<float> m_plugL, m_plugR; // deinterleaved for plugin chain
};
