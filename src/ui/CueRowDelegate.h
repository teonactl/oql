#pragma once
#include <QStyledItemDelegate>
#include "engine/Cue.h"

class CueRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CueRowDelegate(QObject *parent = nullptr);
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
private:
    static QColor cardColor(const QModelIndex &idx, bool isSelected, bool isHovered);
    static QColor badgeColor(Cue::Type t);
    static QString badgeLabel(Cue::Type t);
};
