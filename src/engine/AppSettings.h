#pragma once
#include <QKeySequence>
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

    QKeySequence keyGo()       const;
    void         setKeyGo(const QKeySequence &k);
    QKeySequence keyStopAll()  const;
    void         setKeyStopAll(const QKeySequence &k);
    QKeySequence keyFirstCue() const;
    void         setKeyFirstCue(const QKeySequence &k);

private:
    AppSettings();
    QSettings m_s;
};
