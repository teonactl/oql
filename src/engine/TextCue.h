#pragma once
#include "Cue.h"
#include <QColor>

class TextCue : public Cue {
    Q_OBJECT
public:
    explicit TextCue(QObject *parent = nullptr);
    Type    type()     const override { return Type::Text; }
    QString typeName() const override { return tr("Testo"); }
    void    go()             override;
    void    stop()           override;
    void    pause()          override {}
    double  duration() const override { return 0.0; }

    // Fade visivo (FadeCue): nessun "volume" persistito, solo livello runtime.
    void   setPlaybackVolume(double v) override;
    double visualLevel()       const { return m_visualLevel; }

    QString text()            const { return m_text; }
    QString fontFamily()      const { return m_fontFamily; }
    int     fontSize()        const { return m_fontSize; }
    bool    bold()            const { return m_bold; }
    bool    italic()          const { return m_italic; }
    QColor  textColor()       const { return m_textColor; }
    QColor  backgroundColor() const { return m_backgroundColor; }
    int     alignment()       const { return m_alignment; }

    void setText(const QString &v)           { m_text = v;            emit propertyChanged(); }
    void setFontFamily(const QString &v)     { m_fontFamily = v;      emit propertyChanged(); }
    void setFontSize(int v)                  { m_fontSize = v;        emit propertyChanged(); }
    void setBold(bool v)                     { m_bold = v;            emit propertyChanged(); }
    void setItalic(bool v)                   { m_italic = v;          emit propertyChanged(); }
    void setTextColor(const QColor &v)       { m_textColor = v;       emit propertyChanged(); }
    void setBackgroundColor(const QColor &v) { m_backgroundColor = v; emit propertyChanged(); }
    void setAlignment(int v)                 { m_alignment = v;       emit propertyChanged(); }

    QJsonObject toJson()                  const override;
    void        fromJson(const QJsonObject &o) override;

private:
    QString m_text;
    QString m_fontFamily      = "Sans Serif";
    int     m_fontSize        = 48;
    bool    m_bold            = false;
    bool    m_italic          = false;
    QColor  m_textColor       = Qt::white;
    QColor  m_backgroundColor = Qt::black;
    int     m_alignment       = Qt::AlignCenter;
    double  m_visualLevel     = 1.0;
};
