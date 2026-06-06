#pragma once
#include "Cue.h"

class LabelCue : public Cue {
    Q_OBJECT
public:
    explicit LabelCue(QObject *parent = nullptr);
    Type    type()     const override { return Type::Label; }
    QString typeName() const override { return "Etichetta"; }
    void    go()             override;
    void    stop()           override {}
    void    pause()          override {}
    double  duration() const override { return 0.0; }

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o) override;
};
