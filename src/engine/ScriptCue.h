#pragma once
#include "Cue.h"

class ScriptCue : public Cue {
    Q_OBJECT
public:
    explicit ScriptCue(QObject *parent = nullptr);
    Type    type()     const override { return Type::Script; }
    QString typeName() const override { return tr("Script"); }
    void    go()             override;
    void    stop()           override;
    void    pause()          override {}
    double  duration() const override { return 0.0; }

    QString script() const              { return m_script; }
    void    setScript(const QString &s) { m_script = s; emit propertyChanged(); }

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o) override;

private:
    QString m_script;
};
