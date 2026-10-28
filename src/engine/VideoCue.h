#pragma once
#include "Cue.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
#include <QStringList>
#include <QTimer>
#include <QFutureWatcher>

class VideoCue : public Cue {
    Q_OBJECT
public:
    explicit VideoCue(QObject *parent = nullptr);

    enum class Background { Black, White, Image };

    Type    type()     const override { return Type::Video; }
    QString typeName() const override { return tr("Video"); }

    QString filePath() const { return m_filePath; }
    double  volume()   const { return m_volume; }
    bool    muted()    const { return m_muted; }
    double  visualLevel() const { return m_visualLevel; }

    void setMuted(bool m);

    int  loopCount() const { return m_loopCount; }
    void setLoopCount(int n);

    Background background()         const { return m_background; }
    QString    backgroundImagePath() const { return m_backgroundImagePath; }
    void setBackground(Background b)             { m_background = b; emit propertyChanged(); }
    void setBackgroundImagePath(const QString &p) { m_backgroundImagePath = p; emit propertyChanged(); }

    // Id degli VirtualScreen (engine/VirtualScreen.h) a cui questa cue è
    // assegnata — il contenuto compare solo lì. Lista vuota = nessuna
    // restrizione (si mostra su qualunque schermo, utile come default
    // retrocompatibile per i progetti creati prima di questa funzione).
    QStringList assignedScreenIds() const { return m_assignedScreenIds; }
    void setAssignedScreenIds(const QStringList &ids) { m_assignedScreenIds = ids; emit propertyChanged(); }

    // Fade in/out automatico della cue stessa (come AudioCue::fadeInDuration/
    // fadeOutDuration, ma qui l'inviluppo è calcolato da un timer invece che
    // per-buffer audio: 0 = nessun fade automatico, va in/esce a scatto).
    // Fade esplicito a metà riproduzione via una cue Fade separata (che può
    // scegliere di agire solo sul video o solo sull'audio, vedi FadeCue::
    // FadeChannel) resta indipendente da questo.
    double fadeInDuration()  const { return m_fadeInDuration; }
    double fadeOutDuration() const { return m_fadeOutDuration; }
    void setFadeInDuration(double s)  { m_fadeInDuration  = qMax(0.0, s); emit propertyChanged(); }
    void setFadeOutDuration(double s) { m_fadeOutDuration = qMax(0.0, s); emit propertyChanged(); }

    void setPlaybackRate(double r) override;

    void setFilePath(const QString &path);
    void setVolume(double v);
    void setPlaybackVolume(double v) override;
    // Come setPlaybackVolume(), ma agisce su un solo canale — usate da
    // FadeCue quando l'utente sceglie di far sfumare solo il video o solo
    // l'audio di una cue video invece di entrambi insieme.
    void setAudioPlaybackVolume(double v);
    void setVisualPlaybackVolume(double v);

    // Ultimo frame decodificato, già convertito in QImage — letto (pull)
    // dalle finestre di output a ogni ridisegno invece di spingere i frame
    // verso un singolo QVideoSink esterno: così più finestre (uno schermo
    // virtuale per output fisico) possono mostrare la stessa cue
    // contemporaneamente, o la finestra può scegliere quale cue mostrare
    // per ciascuno schermo senza dover riassegnare un sink condiviso.
    QImage currentFrame() const { return m_lastFrame; }

    void   go()              override;
    void   stop()            override;
    void   pause()           override;
    double duration() const  override;
    double position() const  override;

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o)  override;

signals:
    // Emesso a ogni nuovo frame decodificato — le finestre di output si
    // limitano a chiedere un ridisegno periodico (timer), non serve
    // ascoltarlo per forza, ma è utile per chi vuole reagire subito.
    void frameChanged();

private slots:
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onVideoFrameChanged(const QVideoFrame &frame);
    void onFadeTick();
    void onFrameConversionFinished();

private:
    // Volume effettivamente applicato all'uscita audio, tenendo conto del
    // muto — usata ovunque si ripristina/imposta il volume "di base" (non
    // durante un fade esplicito, che ha la sua stessa logica in
    // setAudioPlaybackVolume()).
    void applyBaseAudioVolume();

    // QVideoFrame::toImage() (conversione di formato, spesso YUV->RGB) non è
    // gratuita (~10-15ms per frame 1080p) ed è stata osservata come causa
    // concreta di scatti quando più VideoCue sono attive insieme: tutte le
    // conversioni altrimenti si accodano sullo stesso thread GUI, arrivando
    // a competere col tempo disponibile per il repaint. Qui si fa partire su
    // un thread del pool, tenendo in coda solo l'ULTIMO frame se arrivano
    // più frame prima che la conversione precedente sia finita — meglio
    // perdere un frame visivo occasionale che accumulare un arretrato.
    // Limitata ai frame "di memoria" (handleType()==NoHandle, il caso
    // comune per un decoder software come VP9/H264 via plugin software): un
    // frame con handle GPU/texture potrebbe richiedere il contesto del
    // thread che l'ha creato per essere mappato in modo sicuro, quindi per
    // quelli si resta sul percorso sincrono di prima (nessun rischio, solo
    // nessun guadagno per un caso che qui non si incontra mai).
    void startAsyncConversion(const QVideoFrame &frame, qint64 ts);
    void convertSync(const QVideoFrame &frame, qint64 ts);

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QVideoSink   *m_sink;
    QTimer       *m_fadeTimer;
    QFutureWatcher<QImage> *m_convertWatcher;
    bool          m_conversionInFlight = false;
    QVideoFrame   m_queuedFrame;
    bool          m_hasQueuedFrame = false;
    qint64        m_pendingFrameStartTime = -1;
    QImage        m_lastFrame;
    // Timestamp nativo (QVideoFrame::startTime(), microsecondi) dell'ultimo
    // frame già convertito in m_lastFrame — alcuni backend QtMultimedia
    // emettono QVideoSink::videoFrameChanged più di una volta per lo stesso
    // frame (es. durante una rinegoziazione di formato); senza questo
    // controllo si ripeteva ogni volta la conversione toImage() (costosa,
    // ~10-15ms per frame 1080p), uno spreco diretto di budget-frame
    // osservato come causa concreta di scatti con più video attivi insieme.
    qint64        m_lastFrameStartTime = -1;
    QString       m_filePath;
    double        m_volume      = 1.0;
    bool          m_muted       = false;
    double        m_visualLevel = 1.0;
    bool          m_restarting = false;
    int           m_loopCount  = 1;   // 0 = infinite
    Background    m_background = Background::Black;
    QString       m_backgroundImagePath;
    QStringList   m_assignedScreenIds;
    double        m_fadeInDuration  = 0.0;
    double        m_fadeOutDuration = 0.0;
};
