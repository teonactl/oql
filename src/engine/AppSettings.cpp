#include "AppSettings.h"

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
