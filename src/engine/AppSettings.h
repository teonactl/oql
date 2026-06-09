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

    bool    webEnabled() const;
    void    setWebEnabled(bool v);
    quint16 webPort()    const;
    void    setWebPort(quint16 p);

private:
    AppSettings();
    QSettings m_s;
};
