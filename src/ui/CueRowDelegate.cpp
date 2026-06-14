#include "CueRowDelegate.h"
#include "CueListModel.h"
#include <QPainter>
#include <QAbstractItemView>

static constexpr int kGap    = 3;
static constexpr int kRadius = 7;

static const QColor kCardDefault(0x1e, 0x23, 0x34);
static const QColor kTextMain  (0xe2, 0xe8, 0xf0);
static const QColor kTextMuted (0x88, 0x92, 0xa4);
static const QColor kSelBlue   (0x4f, 0x8e, 0xf7);

CueRowDelegate::CueRowDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QSize CueRowDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const {
    return QStyledItemDelegate::sizeHint(opt, idx);
}

QColor CueRowDelegate::cardColor(const QModelIndex &idx, bool isSelected, bool isHovered)
{
    const QVariant bgVar = idx.sibling(idx.row(), CueListModel::ColNumber).data(Qt::BackgroundRole);
    QColor base = kCardDefault;
    if (bgVar.canConvert<QColor>()) {
        const QColor c = bgVar.value<QColor>();
        if (c.isValid()) base = c;
    }

    if (isSelected) {
        // 50/50 blend with blue for clearly visible selection
        base = QColor((base.red()   + kSelBlue.red())   / 2,
                      (base.green() + kSelBlue.green()) / 2,
                      (base.blue()  + kSelBlue.blue())  / 2);
    } else if (isHovered) {
        base = base.lighter(118);
    }
    return base;
}

QColor CueRowDelegate::badgeColor(Cue::Type t)
{
    using T = Cue::Type;
    switch (t) {
    case T::Audio:       return QColor(0x1e, 0x40, 0xaf);
    case T::Video:       return QColor(0x06, 0x5f, 0x46);
    case T::Fade:        return QColor(0x92, 0x40, 0x07);
    case T::Stop:        return QColor(0x99, 0x1b, 0x1b);
    case T::Pause:       return QColor(0x78, 0x35, 0x0f);
    case T::Play:        return QColor(0x16, 0x53, 0x34);
    case T::Speed:       return QColor(0x0e, 0x5c, 0x6e);
    case T::Mic:         return QColor(0x6b, 0x21, 0xa8);
    case T::Record:      return QColor(0x9f, 0x12, 0x39);
    case T::Group:       return QColor(0x37, 0x41, 0x51);
    case T::Label:       return QColor(0x6b, 0x5a, 0x0a);
    case T::Text:        return QColor(0x0e, 0x4f, 0x5c);
    case T::Effect:      return QColor(0x5b, 0x21, 0xb6);
    case T::ResetEffect: return QColor(0x1e, 0x40, 0x6e);
    case T::Script:      return QColor(0x16, 0x65, 0x34);
    }
    return QColor(0x37, 0x41, 0x51);
}

QString CueRowDelegate::badgeLabel(Cue::Type t)
{
    using T = Cue::Type;
    switch (t) {
    case T::Audio:       return QStringLiteral("AUDIO");
    case T::Video:       return QStringLiteral("VIDEO");
    case T::Fade:        return QStringLiteral("FADE");
    case T::Stop:        return QStringLiteral("STOP");
    case T::Pause:       return QStringLiteral("PAUSE");
    case T::Play:        return QStringLiteral("PLAY");
    case T::Speed:       return QStringLiteral("SPEED");
    case T::Mic:         return QStringLiteral("MIC");
    case T::Record:      return QStringLiteral("REC");
    case T::Group:       return QStringLiteral("GROUP");
    case T::Label:       return QStringLiteral("LABEL");
    case T::Text:        return QStringLiteral("TEXT");
    case T::Effect:      return QStringLiteral("FX");
    case T::ResetEffect: return QStringLiteral("RESET");
    case T::Script:      return QStringLiteral("SCRIPT");
    }
    return {};
}

void CueRowDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    const int col = idx.column();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // Query the selection model directly — QSS may suppress State_Selected flag.
    const auto *view = qobject_cast<const QAbstractItemView*>(opt.widget);
    const bool isSelected = view && view->selectionModel()
        ? view->selectionModel()->isRowSelected(idx.row(), idx.parent())
        : bool(opt.state & QStyle::State_Selected);
    const bool isHovered = bool(opt.state & QStyle::State_MouseOver);

    // ── Card background — drawn in ColNumber, unclipped, spans full viewport ──
    if (col == CueListModel::ColNumber) {
        const int vw = opt.widget ? opt.widget->width() : opt.rect.right();
        const QRectF card(0, opt.rect.top() + kGap, vw, opt.rect.height() - kGap * 2);

        p->setClipping(false);
        p->setPen(Qt::NoPen);
        p->setBrush(cardColor(idx, isSelected, isHovered));
        p->drawRoundedRect(card, kRadius, kRadius);
        if (isSelected) {
            // Thick blue left bar — unmistakable selection indicator
            p->setBrush(kSelBlue);
            p->drawRoundedRect(QRectF(0, card.top() + 1, 6, card.height() - 2), 2, 2);
        }
        p->setClipping(true);

        // Number — muted, right-aligned
        p->setPen(kTextMuted);
        p->setFont(opt.font);
        p->drawText(opt.rect.adjusted(4, 0, -4, 0),
                    Qt::AlignVCenter | Qt::AlignRight,
                    idx.data(Qt::DisplayRole).toString());
        p->restore();
        return;
    }

    // ── Type badge ─────────────────────────────────────────────────────────────
    if (col == CueListModel::ColType) {
        const Cue::Type type = static_cast<Cue::Type>(idx.data(Qt::UserRole).toInt());
        const QString label  = badgeLabel(type);
        if (!label.isEmpty()) {
            const int bh = 18;
            const int bw = qMin(opt.rect.width() - 6, 52);
            const QRect badge(opt.rect.left() + 3,
                              opt.rect.center().y() - bh / 2,
                              bw, bh);
            p->setPen(Qt::NoPen);
            p->setBrush(badgeColor(type));
            p->drawRoundedRect(badge, 3, 3);

            QFont f = opt.font;
            f.setPointSize(qMax(7, opt.font.pointSize() - 1));
            f.setBold(true);
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(badge, Qt::AlignCenter, label);
        }
        p->restore();
        return;
    }

    // ── All other columns: text only ───────────────────────────────────────────
    QColor textColor = kTextMain;
    const QVariant fg = idx.data(Qt::ForegroundRole);
    if (fg.canConvert<QColor>()) textColor = fg.value<QColor>();
    p->setPen(textColor);

    const QVariant fontVar = idx.data(Qt::FontRole);
    p->setFont(fontVar.canConvert<QFont>() ? fontVar.value<QFont>() : opt.font);

    Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft;
    if (col == CueListModel::ColPreWait
     || col == CueListModel::ColDuration
     || col == CueListModel::ColPostWait) {
        align = Qt::AlignVCenter | Qt::AlignRight;
        p->setPen(kTextMuted);
    } else if (col == CueListModel::ColContinue) {
        align = Qt::AlignCenter;
    }

    p->drawText(opt.rect.adjusted(6, 0, -4, 0), align,
                idx.data(Qt::DisplayRole).toString());
    p->restore();
}
