#pragma once
#include "Cue.h"

class GroupCue : public Cue {
    Q_OBJECT
public:
    explicit GroupCue(QObject *parent = nullptr);
    Type    type()     const override { return Type::Group; }
    QString typeName() const override { return tr("Gruppo"); }
    void    go()             override;
    void    stop()           override {}
    void    pause()          override {}
    double  duration() const override { return 0.0; }

    bool collapsed() const    { return m_collapsed; }
    void setCollapsed(bool v) { m_collapsed = v; emit layoutChanged(); }

    QJsonObject toJson()                 const override;
    void        fromJson(const QJsonObject &o) override;

signals:
    void layoutChanged(); // collapse-state change: view rebuild only, NOT workspace modified

private:
    bool m_collapsed = false;
};
