#include "ImageCue.h"
#include <QJsonArray>
#include <QFileInfo>

ImageCue::ImageCue(QObject *parent) : Cue(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, &ImageCue::onTick);
}

void ImageCue::go() {
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
    const int n = m_imagePaths.size();
    if (n <= 0) return 0.0;
    return n * m_slideDuration + qMax(0, n - 1) * m_transitionDuration;
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
}
