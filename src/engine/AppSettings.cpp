#include "AppSettings.h"
#include <QKeySequence>

AppSettings &AppSettings::instance() {
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings() : m_s("OpenQLab", "OpenQLab") {}

double AppSettings::defaultFadeDuration() const {
    return m_s.value("defaultFadeDuration", 2.0).toDouble();
}
void AppSettings::setDefaultFadeDuration(double s) {
    m_s.setValue("defaultFadeDuration", qMax(0.1, s));
}

double AppSettings::defaultFadeInDuration() const {
    return m_s.value("defaultFadeInDuration", 0.0).toDouble();
}
void AppSettings::setDefaultFadeInDuration(double s) {
    m_s.setValue("defaultFadeInDuration", qMax(0.0, s));
}

double AppSettings::defaultFadeOutDuration() const {
    return m_s.value("defaultFadeOutDuration", 0.0).toDouble();
}
void AppSettings::setDefaultFadeOutDuration(double s) {
    m_s.setValue("defaultFadeOutDuration", qMax(0.0, s));
}

bool AppSettings::autoNumberNewCues() const {
    return m_s.value("autoNumberNewCues", true).toBool();
}
void AppSettings::setAutoNumberNewCues(bool v) {
    m_s.setValue("autoNumberNewCues", v);
}

QKeySequence AppSettings::keyGo() const {
    const QString s = m_s.value("keyGo").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Space) : QKeySequence(s);
}
void AppSettings::setKeyGo(const QKeySequence &k) { m_s.setValue("keyGo", k.toString()); }

QKeySequence AppSettings::keyStopAll() const {
    const QString s = m_s.value("keyStopAll").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Escape) : QKeySequence(s);
}
void AppSettings::setKeyStopAll(const QKeySequence &k) { m_s.setValue("keyStopAll", k.toString()); }

QKeySequence AppSettings::keyFirstCue() const {
    const QString s = m_s.value("keyFirstCue").toString();
    return s.isEmpty() ? QKeySequence(Qt::Key_Home) : QKeySequence(s);
}
void AppSettings::setKeyFirstCue(const QKeySequence &k) { m_s.setValue("keyFirstCue", k.toString()); }
