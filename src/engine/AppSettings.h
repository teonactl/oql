#pragma once
#include <QSettings>

class AppSettings {
public:
    static AppSettings &instance();

    double defaultFadeDuration() const;
    void   setDefaultFadeDuration(double s);

    double defaultFadeInDuration() const;
    void   setDefaultFadeInDuration(double s);

    double defaultFadeOutDuration() const;
    void   setDefaultFadeOutDuration(double s);

    bool   autoNumberNewCues() const;
    void   setAutoNumberNewCues(bool v);

private:
    AppSettings();
    QSettings m_s;
};
