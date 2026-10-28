#include "VideoCue.h"
#include "AppSettings.h"
#include <QFileInfo>
#include <QUrl>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QVideoFrame>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>

// Diagnostica opzionale per gli scatti/disallineamenti segnalati con più
// VideoCue attive insieme: QVideoFrame::toImage() fa una conversione di
// formato (spesso YUV->RGB) non gratuita, ed è chiamata qui sul thread
// GUI/principale a ogni frame del decoder. Con una sola cue attiva il costo
// rientra comodamente nel budget di un frame; con più cue in parallelo i
// tempi di conversione si accodano sullo stesso thread, e questo può sia
// rallentare il repaint (scatti) sia far sì che una cue resti sistematicamente
// un po' indietro rispetto a un'altra avviata insieme (disallineamento
// percepito). Attivabile con OQL_VIDEO_TIMING=1 nell'ambiente per stampare su
// stderr il costo di ogni conversione e la posizione di playback della cue,
// senza bisogno di ricompilare per raccoglierli.
static bool videoTimingEnabled() {
    static const bool enabled = qEnvironmentVariableIntValue("OQL_VIDEO_TIMING") != 0;
    return enabled;
}

VideoCue::VideoCue(QObject *parent) : Cue(parent) {
    m_player      = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);

    // Il sink è di proprietà della cue stessa, non di una finestra di
    // output esterna: i frame vengono "tirati" (currentFrame()) da chi ne
    // ha bisogno invece che spinti verso un unico sink condiviso — vedi il
    // commento in VideoCue.h su perché (più schermi virtuali possono
    // mostrare la stessa cue contemporaneamente).
    m_sink = new QVideoSink(this);
    m_player->setVideoOutput(m_sink);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoCue::onVideoFrameChanged);

    // Il watcher vive sul thread GUI (figlio di `this`): QFutureWatcher
    // garantisce che finished() venga recapitato lì indipendentemente dal
    // thread del pool che ha davvero eseguito la conversione — sicuro
    // toccare m_lastFrame/emettere segnali dentro onFrameConversionFinished.
    m_convertWatcher = new QFutureWatcher<QImage>(this);
    connect(m_convertWatcher, &QFutureWatcher<QImage>::finished, this, &VideoCue::onFrameConversionFinished);

    // Rispetta l'uscita configurata in Impostazioni → Hardware invece di
    // andare sempre sul device di sistema — vedi lo stesso commento in
    // MicCue::go() sul confronto "best effort" per descrizione (miniaudio e
    // QMediaDevices sono due enumerazioni distinte). Letto una volta sola
    // qui (non ad ogni go()): come per AudioCue, un cambio dell'impostazione
    // vale per le cue create/ricaricate dopo, non per quelle già in corso.
    const QString configuredOut = AppSettings::instance().audioOutputDevice();
    if (!configuredOut.isEmpty()) {
        for (const auto &dev : QMediaDevices::audioOutputs()) {
            if (dev.description() == configuredOut) { m_audioOutput->setDevice(dev); break; }
        }
    }

    m_player->setAudioOutput(m_audioOutput);
    applyBaseAudioVolume();

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this,     &VideoCue::onPlaybackStateChanged);

    // Fade in/out automatico (vedi onFadeTick): un timer invece di un
    // calcolo per-buffer come AudioCue, perché qui non c'è un callback
    // audio a cui agganciarsi per il livello visivo — 50ms è più che
    // sufficiente per un fade visivo fluido.
    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setInterval(50);
    connect(m_fadeTimer, &QTimer::timeout, this, &VideoCue::onFadeTick);
}

void VideoCue::setFilePath(const QString &path) {
    m_filePath = path;
    m_player->setSource(QUrl::fromLocalFile(path));
    if (m_name.isEmpty())
        m_name = QFileInfo(path).completeBaseName();
    emit propertyChanged();
}

void VideoCue::setVolume(double v) {
    m_volume = qBound(0.0, v, 1.0);
    applyBaseAudioVolume();
    emit propertyChanged();
}

void VideoCue::setMuted(bool m) {
    if (m_muted == m) return;
    m_muted = m;
    applyBaseAudioVolume();
    emit propertyChanged();
}

void VideoCue::applyBaseAudioVolume() {
    m_audioOutput->setVolume(m_muted ? 0.0f : float(m_volume));
}

void VideoCue::setPlaybackVolume(double v) {
    setAudioPlaybackVolume(v);
    setVisualPlaybackVolume(v);
}

void VideoCue::setAudioPlaybackVolume(double v) {
    const double clamped = qBound(0.0, v, 1.0);
    // Il muto resta un'esclusione a priori: anche durante un fade esplicito
    // (FadeCue) o automatico (fade in/out della cue) una cue mutata deve
    // restare silenziosa.
    m_audioOutput->setVolume(m_muted ? 0.0f : float(clamped));
}

void VideoCue::setVisualPlaybackVolume(double v) {
    const double clamped = qBound(0.0, v, 1.0);
    // Normalizza rispetto al volume base (come AudioCue::m_playbackScale) cosi
    // un fade arriva a opacita visiva 0 anche se il volume base e' < 1.0.
    m_visualLevel = (m_volume > 0.001)
        ? qBound(0.0, 1.0, clamped / m_volume)
        : (clamped > 0.001 ? 1.0 : 0.0);
    emit displayChanged();
}

void VideoCue::onFadeTick() {
    const double dur = duration();
    if (dur <= 0.001) return;
    const double pos = position();
    double env = 1.0;
    if (m_fadeInDuration  > 0.01) env *= qMin(1.0, pos / m_fadeInDuration);
    if (m_fadeOutDuration > 0.01) env *= qMin(1.0, (dur - pos) / m_fadeOutDuration);
    setPlaybackVolume(m_volume * env);
}

void VideoCue::convertSync(const QVideoFrame &frame, qint64 ts) {
    if (videoTimingEnabled()) {
        QElapsedTimer t; t.start();
        m_lastFrame = frame.toImage();
        // this/ts (timestamp nativo del frame, microsecondi) servono a
        // distinguere "due cue diverse con la stessa posizione per
        // coincidenza" da "position() aggiornata più raramente dei frame
        // consegnati" — col solo "pos" i due casi sono indistinguibili.
        qWarning().noquote() << QStringLiteral("[video-timing-sync] cue=%1(\"%2\") ts=%3us toImage=%4ms pos=%5s")
            .arg(quintptr(this), 0, 16).arg(name()).arg(ts).arg(t.elapsed()).arg(position(), 0, 'f', 3);
    } else {
        m_lastFrame = frame.toImage();
    }
    m_lastFrameStartTime = ts;
}

void VideoCue::startAsyncConversion(const QVideoFrame &frame, qint64 ts) {
    m_conversionInFlight = true;
    m_pendingFrameStartTime = ts;
    const bool timing = videoTimingEnabled();
    const QString cueName = name();
    const double pos = position();
    const quintptr self = quintptr(this);
    // `frame` è catturato per valore: copia solo l'handle/i metadati del
    // QVideoFrame (il buffer sottostante è a conteggio di riferimenti),
    // quindi è un'operazione leggera eseguita qui sul thread GUI — il
    // lavoro pesante (toImage(), nel lambda) gira sul thread del pool.
    QFuture<QImage> future = QtConcurrent::run([frame, timing, cueName, pos, self, ts]() mutable {
        QImage img;
        if (timing) {
            QElapsedTimer t; t.start();
            img = frame.toImage();
            qWarning().noquote() << QStringLiteral("[video-timing-async] cue=%1(\"%2\") ts=%3us toImage=%4ms pos=%5s")
                .arg(self, 0, 16).arg(cueName).arg(ts).arg(t.elapsed()).arg(pos, 0, 'f', 3);
        } else {
            img = frame.toImage();
        }
        return img;
    });
    m_convertWatcher->setFuture(future);
}

void VideoCue::onFrameConversionFinished() {
    m_lastFrame = m_convertWatcher->result();
    m_lastFrameStartTime = m_pendingFrameStartTime;
    m_conversionInFlight = false;
    emit frameChanged();

    if (m_hasQueuedFrame) {
        const QVideoFrame next = m_queuedFrame;
        m_queuedFrame = QVideoFrame();
        m_hasQueuedFrame = false;
        const qint64 nextTs = next.startTime();
        if (nextTs < 0 || nextTs != m_lastFrameStartTime)
            startAsyncConversion(next, nextTs);
    }
}

void VideoCue::onVideoFrameChanged(const QVideoFrame &frame) {
    if (!frame.isValid()) { emit frameChanged(); return; }

    const qint64 ts = frame.startTime();
    // Alcuni backend ri-emettono lo stesso frame (stesso startTime) più
    // volte di seguito: evitare di rifare toImage() per un frame già
    // convertito è un costo risparmiato del tutto gratis — vedi il
    // commento su m_lastFrameStartTime in VideoCue.h.
    if (ts >= 0 && ts == m_lastFrameStartTime) { emit frameChanged(); return; }

    // Un frame con handle GPU/texture (o senza timestamp valido, raro)
    // resta sul percorso sincrono di prima — vedi il commento in
    // VideoCue.h su startAsyncConversion().
    if (ts < 0 || frame.handleType() != QVideoFrame::NoHandle) {
        convertSync(frame, ts);
        emit frameChanged();
        return;
    }

    if (m_conversionInFlight) {
        // Si tiene solo l'ULTIMO frame in attesa: meglio perdere un frame
        // visivo occasionale che accumulare un arretrato di conversioni.
        m_queuedFrame = frame;
        m_hasQueuedFrame = true;
        return;
    }
    startAsyncConversion(frame, ts);
}

void VideoCue::setLoopCount(int n) {
    m_loopCount = qMax(0, n);
    emit propertyChanged();
}

void VideoCue::setPlaybackRate(double r) {
    m_player->setPlaybackRate(r);
}

void VideoCue::go() {
    if (m_state == State::Paused) {
        m_player->play();
        setState(State::Playing);
        return;
    }
    m_player->setLoops(m_loopCount == 0 ? QMediaPlayer::Infinite : m_loopCount);
    m_player->setPlaybackRate(1.0);
    m_restarting = true;
    m_player->stop();
    m_restarting = false;

    m_visualLevel = 1.0;
    m_player->play();
    setState(State::Playing);
    if (m_fadeInDuration > 0.01 || m_fadeOutDuration > 0.01) m_fadeTimer->start();
    emit displayChanged();
}

void VideoCue::stop() {
    m_fadeTimer->stop();
    m_restarting = true;
    m_player->stop();
    m_restarting = false;
    applyBaseAudioVolume();
    m_visualLevel = 1.0;
    setState(State::Idle);
    emit displayChanged();
}

void VideoCue::pause() {
    m_player->pause();
    setState(State::Paused);
}

double VideoCue::duration() const { return m_player->duration() / 1000.0; }
double VideoCue::position() const { return m_player->position() / 1000.0; }

void VideoCue::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::StoppedState && m_state == State::Playing && !m_restarting) {
        m_fadeTimer->stop();
        applyBaseAudioVolume();
        m_visualLevel = 1.0;
        setState(State::Idle);
        emit displayChanged();
        emit finished();
    }
}

QJsonObject VideoCue::toJson() const {
    auto obj         = Cue::toJson();
    obj["cueType"]   = "video";
    obj["filePath"]  = m_filePath;
    obj["volume"]    = m_volume;
    obj["muted"]     = m_muted;
    obj["loopCount"] = m_loopCount;
    obj["background"]      = static_cast<int>(m_background);
    obj["backgroundImage"] = m_backgroundImagePath;
    obj["assignedScreens"] = QJsonArray::fromStringList(m_assignedScreenIds);
    obj["fadeIn"]  = m_fadeInDuration;
    obj["fadeOut"] = m_fadeOutDuration;
    return obj;
}

void VideoCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);
    const QString path = o["filePath"].toString();
    if (!path.isEmpty()) setFilePath(path);
    m_muted = o["muted"].toBool(false); // prima di setVolume(): applyBaseAudioVolume() lo deve già vedere
    setVolume(o["volume"].toDouble(1.0));
    m_loopCount = o["loopCount"].toInt(1);
    m_background = static_cast<Background>(o["background"].toInt(
                        static_cast<int>(Background::Black)));
    m_backgroundImagePath = o["backgroundImage"].toString();
    m_assignedScreenIds.clear();
    for (const QJsonValue &v : o["assignedScreens"].toArray())
        m_assignedScreenIds.append(v.toString());
    m_fadeInDuration  = qMax(0.0, o["fadeIn"].toDouble(0.0));
    m_fadeOutDuration = qMax(0.0, o["fadeOut"].toDouble(0.0));
}
