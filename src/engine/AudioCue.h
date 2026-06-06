#pragma once
#include "Cue.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QTimer>
#include <QVector>
#include <QPointF>

class AudioCue : public Cue {
    Q_OBJECT
public:
    enum class ChannelRoute { Both = 0, Left, Right };

    explicit AudioCue(QObject *parent = nullptr);

    Type    type()     const override { return Type::Audio; }
    QString typeName() const override { return "Audio"; }

    QString filePath()        const { return m_filePath; }
    double  volume()          const { return m_volume; }
    double  fadeInDuration()  const { return m_fadeIn; }
    double  fadeOutDuration() const { return m_fadeOut; }
    ChannelRoute channelRoute()       const { return m_channel; }
    QVector<QPointF> volumePoints()   const { return m_volumePoints; }
    double trimStart() const { return m_trimStart; }  // seconds
    double trimEnd()   const { return m_trimEnd;   }  // seconds (0 = not set)

    void setFilePath(const QString &path);
    void setVolume(double v);
    void setPlaybackVolume(double v) override;
    void setFadeInDuration(double s)  { m_fadeIn  = s; emit propertyChanged(); }
    void setFadeOutDuration(double s) { m_fadeOut = s; emit propertyChanged(); }
    void setChannelRoute(ChannelRoute r) { m_channel = r; emit propertyChanged(); }
    void setVolumePoints(const QVector<QPointF> &pts) { m_volumePoints = pts; emit propertyChanged(); }
    void setTrimStart(double secs) { m_trimStart = qMax(0.0, secs); emit propertyChanged(); }
    void setTrimEnd(double secs)   { m_trimEnd   = qMax(0.0, secs); emit propertyChanged(); }

    int  loopCount() const { return m_loopCount; }
    void setLoopCount(int n) { m_loopCount = qMax(0, n); emit propertyChanged(); }

    void setPlaybackRate(double r) override { m_player->setPlaybackRate(r); }

    void   go()              override;
    void   stop()            override;
    void   pause()           override;
    double duration() const  override;
    double position() const  override;

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private slots:
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onFadeTick();
    void onAutomationTick();

private:
    bool   shouldLoop()    const;
    void   restartForLoop();
    double interpolateVolume(double normT) const;
    double computeFadeVol(double normT)    const;

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QTimer       *m_fadeTimer;
    QTimer       *m_automationTimer;

    QString          m_filePath;
    double           m_volume        = 1.0;
    double           m_playbackScale = 1.0;  // external multiplier set by FadeCue via setPlaybackVolume
    double           m_fadeIn        = 3.0;
    double           m_fadeOut       = 3.0;
    double           m_trimStart     = 0.0;
    double           m_trimEnd       = 0.0;
    double           m_currentVol    = 1.0;
    double           m_fadeStep      = 0.0;
    bool             m_fadingOut     = false;
    bool             m_restarting    = false;
    int              m_loopCount     = 1;   // 0 = infinite, N = play N times
    int              m_loopsRemaining = 1;
    ChannelRoute     m_channel       = ChannelRoute::Both;
    QVector<QPointF> m_volumePoints;
};
