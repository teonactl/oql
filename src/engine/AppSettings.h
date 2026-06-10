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

private:
    AppSettings();
    QSettings m_s;
};
