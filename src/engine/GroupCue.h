#pragma once
#include "Cue.h"

class GroupCue : public Cue {
    Q_OBJECT
public:
    explicit GroupCue(QObject *parent = nullptr);
    Type    type()     const override { return Type::Group; }
    QString typeName() const override { return "Gruppo"; }
    void    go()             override;
    void    stop()           override {}
    void    pause()          override {}
    double  duration() const override { return 0.0; }

    bool collapsed() const    { return m_collapsed; }
    void setCollapsed(bool v) { m_collapsed = v; emit propertyChanged(); }

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o) override;

private:
    bool m_collapsed = false;
};
