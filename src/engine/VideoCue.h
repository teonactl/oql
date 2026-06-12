#pragma once
#include "Cue.h"
#include <QMediaPlayer>
#include <QAudioOutput>

class QVideoSink;

class VideoCue : public Cue {
    Q_OBJECT
public:
    explicit VideoCue(QObject *parent = nullptr);

    Type    type()     const override { return Type::Video; }
    QString typeName() const override { return tr("Video"); }

    QString filePath() const { return m_filePath; }
    double  volume()   const { return m_volume; }

    int  loopCount() const { return m_loopCount; }
    void setLoopCount(int n);

    void setPlaybackRate(double r) override;

    void setFilePath(const QString &path);
    void setVolume(double v);
    void setPlaybackVolume(double v) override;
    void setVideoSink(QVideoSink *sink);

    void   go()              override;
    void   stop()            override;
    void   pause()           override;
    double duration() const  override;
    double position() const  override;

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private slots:
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QString       m_filePath;
    double        m_volume     = 1.0;
    bool          m_restarting = false;
    int           m_loopCount  = 1;   // 0 = infinite
};
