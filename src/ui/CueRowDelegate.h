#pragma once
#include <QStyledItemDelegate>
#include "engine/Cue.h"

class CueRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CueRowDelegate(QObject *parent = nullptr);
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    void setUltraDark(bool on) { m_ultraDark = on; }
    bool isUltraDark() const   { return m_ultraDark; }
private:
    static QColor cardColor(const QModelIndex &idx, bool isSelected, bool isHovered, bool ultraDark);
    static QColor badgeColor(Cue::Type t, bool ultraDark);
    static QString badgeLabel(Cue::Type t);
    bool m_ultraDark = false;
};
