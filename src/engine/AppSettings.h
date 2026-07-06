#pragma once
#include <QKeySequence>
#include <QSettings>
#include <QString>

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

    QList<int> cueListColumnWidths() const;
    void       setCueListColumnWidths(const QList<int> &widths);

    int  cueListRowHeight() const;
    void setCueListRowHeight(int h);

    int     cueListFontSize()   const;
    void    setCueListFontSize(int pt);
    QString cueListFontFamily() const;
    void    setCueListFontFamily(const QString &family);

    // UI layout
    int  activeCuePanelSide() const;   // 0=left, 1=right
    void setActiveCuePanelSide(int side);

    QKeySequence keyShowMode() const;
    void         setKeyShowMode(const QKeySequence &k);

    // Language: "it" (default), "en", "es", "fr"
    QString appLanguage() const;
    void    setAppLanguage(const QString &lang);
    static void applyLanguage();   // (re)installs the QTranslator for the current language

    // Waveform resolution in buckets (1000/2000/4000/8000/16000)
    int  waveformBuckets() const;
    void setWaveformBuckets(int b);

    // Add-cue shortcuts (typeKey: "audio","video","stop","fade","pause","mic","record","group","label","effect","script","text","play","speed")
    QKeySequence keyAddCue(const QString &typeKey) const;
    void         setKeyAddCue(const QString &typeKey, const QKeySequence &k);

    // Plugin search paths (extra, appended to system defaults)
    QStringList lv2ExtraPaths() const;
    void        setLv2ExtraPaths(const QStringList &paths);
    QStringList vstExtraPaths() const;
    void        setVstExtraPaths(const QStringList &paths);

    // Hardware: audio output device (empty = system default)
    QString audioOutputDevice() const;
    void    setAudioOutputDevice(const QString &name);

    // Hardware: video/image/text output screen (empty = current screen)
    QString outputScreenName() const;
    void    setOutputScreenName(const QString &name);

private:
    AppSettings();
    QSettings m_s;
};
