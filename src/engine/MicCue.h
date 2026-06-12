#pragma once
#include "Cue.h"
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>

class MicCue : public Cue {
    Q_OBJECT
public:
    explicit MicCue(QObject *parent = nullptr);
    ~MicCue() override;

    Type    type()     const override { return Type::Mic; }
    QString typeName() const override { return tr("Microfono"); }

    void   go()              override;
    void   stop()            override;
    void   pause()           override;
    double duration() const  override { return 0.0; }
    double volume()   const  override { return m_volume; }

    void setVolume(double v);
    void setPlaybackVolume(double v) override;

    QString inputDeviceId() const { return m_inputDeviceId; }
    void    setInputDeviceId(const QString &id);

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

private:
    void teardown();

    QAudioSource *m_source     = nullptr;
    QAudioSink   *m_sink       = nullptr;
    QIODevice    *m_ioDevice   = nullptr;
    double        m_volume     = 1.0;
    QString       m_inputDeviceId;
};
