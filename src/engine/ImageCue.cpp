#include "ImageCue.h"
#include <QJsonArray>
#include <QFileInfo>
#include <QPainter>
#include <QLinearGradient>
#include <cmath>

ImageCue::ImageCue(QObject *parent) : Cue(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &ImageCue::onTick);
}

void ImageCue::go() {
    if (m_contentMode != ContentMode::File) {
        // Colore pieno o gradiente: nessuna slideshow/timer, il contenuto
        // resta fisso finché la cue non viene fermata manualmente (stesso
        // comportamento di un fondale statico).
        m_phase        = Phase::Holding;
        m_phaseElapsed = 0.0;
        m_visualLevel  = 1.0;
        setState(State::Playing);
        emit displayChanged();
        return;
    }
    if (m_imagePaths.isEmpty()) {
        setState(State::Idle);
        emit finished();
        return;
    }
    m_currentIndex = 0;
    m_targetIndex  = 0;
    m_phase        = Phase::Holding;
    m_phaseElapsed = 0.0;
    m_visualLevel  = 1.0;
    setState(State::Playing);
    m_timer->start();
    emit displayChanged();
}

void ImageCue::stop() {
    m_timer->stop();
    m_visualLevel = 1.0;
    setState(State::Idle);
    emit displayChanged();
}

void ImageCue::finishNaturally() {
    m_timer->stop();
    setState(State::Idle);
    emit finished();
}

void ImageCue::setPlaybackVolume(double v) {
    m_visualLevel = qBound(0.0, v, 1.0);
    emit displayChanged();
}

double ImageCue::duration() const {
    if (m_contentMode != ContentMode::File) return 0.0; // durata indefinita, stop manuale
    const int n = m_imagePaths.size();
    if (n <= 0) return 0.0;
    return n * m_slideDuration + qMax(0, n - 1) * m_transitionDuration;
}

QImage ImageCue::renderGeneratedContent(QSize size) const {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    if (size.isEmpty()) return img;

    if (m_contentMode == ContentMode::SolidColor) {
        img.fill(m_solidColor);
        return img;
    }
    if (m_contentMode == ContentMode::Gradient) {
        QPainter p(&img);
        const double w = size.width(), h = size.height();
        const double rad = m_gradientAngle * M_PI / 180.0;
        const double cx = w / 2.0, cy = h / 2.0;
        // Segmento centrato sul rettangolo, lungo quanto la diagonale: così
        // il gradiente copre sempre l'intera immagine qualunque sia l'angolo.
        const double half = std::hypot(w, h) / 2.0;
        const double dx = std::cos(rad) * half;
        const double dy = std::sin(rad) * half;
        QLinearGradient grad(cx - dx, cy - dy, cx + dx, cy + dy);
        grad.setColorAt(0.0, m_gradientColor1);
        grad.setColorAt(1.0, m_gradientColor2);
        p.fillRect(img.rect(), grad);
        return img;
    }
    img.fill(Qt::black);
    return img;
}

QString ImageCue::currentImagePath() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_imagePaths.size()) return {};
    return m_imagePaths.at(m_currentIndex);
}

QString ImageCue::nextImagePath() const {
    if (m_phase != Phase::Transitioning) return {};
    if (m_targetIndex < 0 || m_targetIndex >= m_imagePaths.size()) return {};
    return m_imagePaths.at(m_targetIndex);
}

double ImageCue::transitionProgress() const {
    if (m_phase != Phase::Transitioning) return 0.0;
    const double effDur = (m_transition == Transition::Cut) ? 0.0 : m_transitionDuration;
    if (effDur <= 0.0001) return 1.0;
    return qBound(0.0, m_phaseElapsed / effDur, 1.0);
}

void ImageCue::onTick() {
    const double dt = m_timer->interval() / 1000.0;
    const int n = m_imagePaths.size();
    if (n <= 0) { finishNaturally(); return; }

    const bool wasTransitioning = (m_phase == Phase::Transitioning);
    m_phaseElapsed += dt;

    if (m_phase == Phase::Holding) {
        if (m_phaseElapsed >= m_slideDuration) {
            const int next = m_currentIndex + 1;
            if (next >= n) {
                if (m_loop) {
                    m_targetIndex  = 0;
                    m_phase        = Phase::Transitioning;
                    m_phaseElapsed = 0.0;
                } else {
                    finishNaturally();
                    return;
                }
            } else {
                m_targetIndex  = next;
                m_phase        = Phase::Transitioning;
                m_phaseElapsed = 0.0;
            }
        }
    } else {
        const double effDur = (m_transition == Transition::Cut) ? 0.0 : m_transitionDuration;
        if (m_phaseElapsed >= effDur) {
            m_currentIndex = m_targetIndex;
            m_phase        = Phase::Holding;
            m_phaseElapsed = 0.0;
        }
    }

    // E4: durante Holding non cambia nulla di visibile tick dopo tick
    // (l'immagine resta la stessa finché non scatta la transizione), ma
    // prima si emetteva displayChanged() comunque — 30 volte al secondo,
    // per l'intera durata della slideshow. Quell'emissione passa da
    // Cue::displayChanged() a CueList::cueLayoutChanged() a
    // CueListModel::onCuePropertyChanged(), innescando un ricalcolo delle
    // righe visibili (E3) ad ogni tick senza motivo. Si emette solo mentre
    // si è davvero in transizione (la dissolvenza/slide avanza ad ogni
    // tick) o nel tick in cui è appena finita (per mostrare lo stato finale).
    if (m_phase == Phase::Transitioning || wasTransitioning)
        emit displayChanged();
}

QJsonObject ImageCue::toJson() const {
    auto obj = Cue::toJson();
    obj["cueType"] = "image";

    QJsonArray images;
    for (const auto &path : m_imagePaths)
        images.append(path);
    obj["images"] = images;

    obj["slideDuration"]      = m_slideDuration;
    obj["transitionDuration"] = m_transitionDuration;
    obj["transition"]         = static_cast<int>(m_transition);
    obj["loop"]                = m_loop;
    obj["background"]         = static_cast<int>(m_background);
    obj["backgroundImage"]    = m_backgroundImagePath;

    obj["contentMode"]    = static_cast<int>(m_contentMode);
    obj["solidColor"]     = m_solidColor.name(QColor::HexArgb);
    obj["gradientColor1"] = m_gradientColor1.name(QColor::HexArgb);
    obj["gradientColor2"] = m_gradientColor2.name(QColor::HexArgb);
    obj["gradientAngle"]  = m_gradientAngle;
    obj["assignedScreens"] = QJsonArray::fromStringList(m_assignedScreenIds);
    return obj;
}

void ImageCue::fromJson(const QJsonObject &o) {
    Cue::fromJson(o);

    m_imagePaths.clear();
    for (const auto &v : o["images"].toArray())
        m_imagePaths.append(v.toString());

    m_slideDuration      = o["slideDuration"].toDouble(3.0);
    m_transitionDuration = o["transitionDuration"].toDouble(1.0);
    m_transition          = static_cast<Transition>(o["transition"].toInt(
                                 static_cast<int>(Transition::Crossfade)));
    m_loop                 = o["loop"].toBool(false);
    m_background           = static_cast<Background>(o["background"].toInt(
                                  static_cast<int>(Background::Black)));
    m_backgroundImagePath  = o["backgroundImage"].toString();

    m_contentMode = static_cast<ContentMode>(o["contentMode"].toInt(
                        static_cast<int>(ContentMode::File)));
    m_solidColor     = QColor(o["solidColor"].toString());
    if (!m_solidColor.isValid()) m_solidColor = Qt::black;
    m_gradientColor1 = QColor(o["gradientColor1"].toString());
    if (!m_gradientColor1.isValid()) m_gradientColor1 = Qt::black;
    m_gradientColor2 = QColor(o["gradientColor2"].toString());
    if (!m_gradientColor2.isValid()) m_gradientColor2 = Qt::white;
    m_gradientAngle  = o["gradientAngle"].toDouble(0.0);
    m_assignedScreenIds.clear();
    for (const QJsonValue &v : o["assignedScreens"].toArray())
        m_assignedScreenIds.append(v.toString());
}
