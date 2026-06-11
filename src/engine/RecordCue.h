#pragma once
#include "Cue.h"
#include <QAudioSource>
#include <QAudioFormat>

class RecordCue : public Cue {
    Q_OBJECT
public:
    explicit RecordCue(QObject *parent = nullptr);
    ~RecordCue() override;

    Type    type()     const override { return Type::Record; }
    QString typeName() const override { return "Registrazione"; }

    void   go()              override;
    void   stop()            override;
    void   pause()           override {}
    double duration() const  override { return 0.0; }
    double volume()   const  override { return 1.0; }
    void   setPlaybackVolume(double)  override {}

    QString inputDeviceId()      const { return m_inputDeviceId; }
    QString linkedAudioCueId()   const { return m_linkedAudioCueId; }
    QString lastRecordingPath()  const { return m_lastRecordingPath; }

    void setInputDeviceId(const QString &id)     { m_inputDeviceId    = id; emit propertyChanged(); }
    void setLinkedAudioCueId(const QString &id)  { m_linkedAudioCueId = id; emit propertyChanged(); }

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o) override;

signals:
    void recordingFinished(const QString &filePath);
    // Emitted so CueList can route the file path to the target AudioCue
    void requestSetFilePath(const QString &audioCueId, const QString &filePath);

private slots:
    void onAudioDataReady();

private:
    void startRecording();
    void stopRecording();
    void writeWavFile(const QString &path);
    static QString makeOutputPath();

    QAudioSource *m_source    = nullptr;
    QIODevice    *m_ioDevice  = nullptr;
    QByteArray    m_buffer;
    QAudioFormat  m_format;

    QString m_inputDeviceId;
    QString m_linkedAudioCueId;
    QString m_lastRecordingPath;
};
