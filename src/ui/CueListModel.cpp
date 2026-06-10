#include "CueListModel.h"
#include "engine/AudioCue.h"
#include "engine/VideoCue.h"
#include "engine/ControlCues.h"
#include "engine/MicCue.h"
#include "engine/GroupCue.h"
#include "engine/LabelCue.h"
#include "engine/TextCue.h"
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSet>

static QIcon makeCueIcon(Cue::Type type) {
    static QHash<int, QIcon> cache;
    const int key = static_cast<int>(type);
    if (cache.contains(key)) return cache[key];

    QPixmap px(16, 16);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg;
    switch (type) {
    case Cue::Type::Audio: bg = QColor(0x2a, 0x6d, 0xcc); break;
    case Cue::Type::Video: bg = QColor(0x2a, 0x88, 0x44); break;
    case Cue::Type::Stop:  bg = QColor(0xcc, 0x33, 0x33); break;
    case Cue::Type::Fade:  bg = QColor(0xcc, 0x77, 0x22); break;
    case Cue::Type::Pause: bg = QColor(0x88, 0x55, 0xcc); break;
    case Cue::Type::Speed: bg = QColor(0x11, 0x99, 0x99); break;
    case Cue::Type::Play:  bg = QColor(0x22, 0xaa, 0x55); break;
    case Cue::Type::Mic:   bg = QColor(0xcc, 0x22, 0x88); break;
    case Cue::Type::Group: bg = QColor(0x44, 0x48, 0x58); break;
    case Cue::Type::Label: bg = QColor(0x60, 0x55, 0x10); break;
    case Cue::Type::Text:        bg = QColor(0x0a, 0x72, 0x8a); break;
    case Cue::Type::Effect:      bg = QColor(0x7b, 0x35, 0x9e); break;
    case Cue::Type::ResetEffect: bg = QColor(0x35, 0x7b, 0x9e); break;
    case Cue::Type::Script:      bg = QColor(0x5a, 0x8a, 0x3a); break;
    default: break;
    }
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, 16, 16, 3, 3);

    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    switch (type) {
    case Cue::Type::Audio: {
        QPolygon tri;
        tri << QPoint(4, 2) << QPoint(4, 14) << QPoint(13, 8);
        p.drawPolygon(tri);
        break;
    }
    case Cue::Type::Video: {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawRoundedRect(2, 3, 12, 10, 2, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygon tri;
        tri << QPoint(5, 5) << QPoint(5, 11) << QPoint(11, 8);
        p.drawPolygon(tri);
        break;
    }
    case Cue::Type::Stop:
        p.drawRect(3, 3, 10, 10);
        break;
    case Cue::Type::Fade: {
        QPolygon env;
        env << QPoint(2, 14) << QPoint(14, 2) << QPoint(14, 14);
        p.drawPolygon(env);
        break;
    }
    case Cue::Type::Pause:
        p.drawRect(3, 3, 4, 10);
        p.drawRect(9, 3, 4, 10);
        break;
    case Cue::Type::Speed: {
        QPolygon t1, t2;
        t1 << QPoint(2, 4) << QPoint(2, 12) << QPoint(7, 8);
        t2 << QPoint(8, 4) << QPoint(8, 12) << QPoint(13, 8);
        p.drawPolygon(t1);
        p.drawPolygon(t2);
        p.drawRect(14, 4, 2, 8);
        break;
    }
    case Cue::Type::Play: {
        p.drawRect(2, 2, 2, 12);
        QPolygon tri;
        tri << QPoint(6, 2) << QPoint(6, 14) << QPoint(14, 8);
        p.drawPolygon(tri);
        break;
    }
    case Cue::Type::Mic: {
        p.setBrush(Qt::white);
        p.drawRoundedRect(5, 1, 6, 9, 3, 3);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawArc(3, 5, 10, 8, 0, -180 * 16);
        p.drawLine(8, 13, 8, 15);
        p.drawLine(5, 15, 11, 15);
        break;
    }
    case Cue::Type::Group: {
        // Folder: tab + body
        p.setPen(Qt::NoPen);
        p.drawRect(1, 4, 5, 2);
        p.drawRect(1, 5, 14, 9);
        break;
    }
    case Cue::Type::Label: {
        // Three text lines
        p.setPen(QPen(Qt::white, 1.5));
        p.drawLine(3, 5,  13, 5);
        p.drawLine(3, 8,  13, 8);
        p.drawLine(3, 11, 10, 11);
        break;
    }
    case Cue::Type::Text: {
        // Bold "T"
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRect(2, 2, 12, 3);   // top bar of T
        p.drawRect(6, 2, 4, 12);   // stem of T
        break;
    }
    case Cue::Type::Effect: {
        // Lightning bolt: zap shape
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygon bolt;
        bolt << QPoint(9, 1) << QPoint(4, 9) << QPoint(8, 9)
             << QPoint(6, 15) << QPoint(12, 7) << QPoint(8, 7);
        p.drawPolygon(bolt);
        break;
    }
    case Cue::Type::ResetEffect: {
        // Circular undo arrow
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawArc(2, 2, 12, 12, 50*16, 250*16);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygon arr;
        arr << QPoint(2, 5) << QPoint(7, 2) << QPoint(7, 8);
        p.drawPolygon(arr);
        break;
    }
    default: break;
    }

    cache[key] = QIcon(px);
    return cache[key];
}

CueListModel::CueListModel(CueList *list, QObject *parent)
    : QAbstractTableModel(parent), m_list(list)
{
    connect(list, &CueList::aboutToReset,    this, &CueListModel::beginResetModel);
    connect(list, &CueList::listReset, this, [this] {
        rebuildVisibleRows();
        endResetModel();
    });
    connect(list, &CueList::cueAdded,           this, &CueListModel::onCueAdded);
    connect(list, &CueList::cueRemoved,         this, &CueListModel::onCueRemoved);
    connect(list, &CueList::cueMoved,           this, &CueListModel::onCueMoved);
    connect(list, &CueList::cueStateChanged,    this, &CueListModel::onCueStateChanged);
    connect(list, &CueList::cuePropertyChanged, this, &CueListModel::onCuePropertyChanged);
    connect(list, &CueList::cueLayoutChanged,   this, &CueListModel::onCuePropertyChanged);
    connect(list, &CueList::playheadChanged,    this, &CueListModel::onPlayheadChanged);
    rebuildVisibleRows();
}

QVector<int> CueListModel::computeVisibleRows() const {
    // Collect collapsed group IDs and existing group IDs
    QSet<QString> collapsed;
    QSet<QString> groupIds;
    for (int i = 0; i < m_list->count(); ++i) {
        const Cue *c = m_list->cueAt(i);
        if (c->type() == Cue::Type::Group) {
            groupIds.insert(c->id());
            if (const auto *gc = dynamic_cast<const GroupCue*>(c); gc && gc->collapsed())
                collapsed.insert(gc->id());
        }
    }

    // Split into roots (no parent, or parent group no longer exists) and children
    QMap<QString, QVector<int>> children; // groupId -> actual indices
    QVector<int> roots;
    for (int i = 0; i < m_list->count(); ++i) {
        const Cue *c = m_list->cueAt(i);
        const QString pgid = c->parentGroupId();
        if (!pgid.isEmpty() && groupIds.contains(pgid))
            children[pgid].append(i);
        else
            roots.append(i);
    }

    // Build visible list: each root in order, followed immediately by its children
    QVector<int> rows;
    for (int actualIdx : roots) {
        rows.append(actualIdx);
        const Cue *c = m_list->cueAt(actualIdx);
        if (c->type() == Cue::Type::Group && !collapsed.contains(c->id())) {
            for (int childIdx : children.value(c->id()))
                rows.append(childIdx);
        }
    }
    return rows;
}

void CueListModel::rebuildVisibleRows() {
    m_visibleRows = computeVisibleRows();
}

void CueListModel::toggleGroupAt(int visibleRow) {
    if (visibleRow < 0 || visibleRow >= m_visibleRows.size()) return;
    Cue *c = m_list->cueAt(m_visibleRows[visibleRow]);
    auto *gc = dynamic_cast<GroupCue*>(c);
    if (!gc) return;
    gc->blockSignals(true);
    gc->setCollapsed(!gc->collapsed());
    gc->blockSignals(false);
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

int CueListModel::rowCount(const QModelIndex &) const    { return m_visibleRows.size(); }
int CueListModel::columnCount(const QModelIndex &) const { return ColCount; }

Cue* CueListModel::cueForRow(int row) const {
    if (row < 0 || row >= m_visibleRows.size()) return nullptr;
    return m_list->cueAt(m_visibleRows[row]);
}

static QString fmtSec(double s) {
    if (s < 0.001) return {};
    return QString::number(s, 'f', 2) + "s";
}

static double parseSeconds(const QString &str) {
    QString s = str.trimmed();
    if (s.endsWith('s', Qt::CaseInsensitive)) s.chop(1);
    bool ok;
    const double v = s.toDouble(&ok);
    return ok ? qMax(0.0, v) : -1.0;
}

QVariant CueListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid()) return {};
    Cue *cue = cueForRow(idx.row());
    if (!cue) return {};

    const bool isPlaying = (cue->state() == Cue::State::Playing);
    const bool isPaused  = (cue->state() == Cue::State::Paused);
    const bool isGroup   = (cue->type() == Cue::Type::Group);
    const bool isLabel   = (cue->type() == Cue::Type::Label);
    const bool isText    = (cue->type() == Cue::Type::Text);

    if (role == Qt::DecorationRole && idx.column() == ColType)
        return makeCueIcon(cue->type());

    if (role == Qt::EditRole) {
        switch (idx.column()) {
        case ColNumber:   return cue->number();
        case ColName:     return cue->name();
        case ColPreWait:  return QString::number(cue->preWait(),  'f', 2);
        case ColPostWait: return QString::number(cue->postWait(), 'f', 2);
        case ColDuration: {
            if (const auto *ac = dynamic_cast<const AudioCue*>(cue))
                return QString::number(ac->trimEnd() > 0.001 ? ac->trimEnd() : ac->duration(), 'f', 2);
            if (const auto *fc = dynamic_cast<const FadeCue*>(cue))
                return QString::number(fc->fadeDuration(), 'f', 2);
            if (const auto *ec = dynamic_cast<const EffectCue*>(cue))
                return ec->effectDuration() > 0.0 ? QString::number(ec->effectDuration(), 'f', 2) : QString{};
            return {};
        }
        }
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case ColNumber: return cue->number();
        case ColType:   return cue->typeName();
        case ColName: {
            if (const auto *gc = dynamic_cast<const GroupCue*>(cue)) {
                const QString arrow = gc->collapsed() ? "▶  " : "▼  ";
                return arrow + (cue->name().isEmpty() ? "(gruppo)" : cue->name());
            }
            const bool isChild = !cue->parentGroupId().isEmpty();
            const QString indent = isChild ? "    └  " : QString{};
            return indent + (cue->name().isEmpty() ? "(senza nome)" : cue->name());
        }
        case ColPreWait:  return fmtSec(cue->preWait());
        case ColDuration: {
            if (const auto *ac = dynamic_cast<const AudioCue*>(cue))
                return fmtSec(ac->trimEnd() > 0.001 ? ac->trimEnd() : ac->duration());
            return fmtSec(cue->duration());
        }
        case ColPostWait: return fmtSec(cue->postWait());
        case ColContinue:
            if (cue->autoContinue()) return QStringLiteral("▶");
            if (cue->autoFollow())   return QStringLiteral("⏭");
            return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        if (isPlaying) return QColor(80, 220, 120);
        if (isPaused)  return QColor(240, 200, 60);
        if (isGroup)   return QColor(210, 210, 225);
        if (isLabel)   return QColor(230, 210, 100);
        if (isText)    return QColor(130, 220, 240);
        if (cue->userColor().isValid())
            return cue->userColor().lightnessF() > 0.5 ? QColor(20, 20, 20) : Qt::white;
    }

    if (role == Qt::FontRole) {
        QFont f;
        bool modified = false;
        if (isPlaying || isPaused || isGroup) { f.setBold(true);   modified = true; }
        if (isLabel || isText)                { f.setItalic(true); modified = true; }
        return modified ? QVariant(f) : QVariant{};
    }

    if (role == Qt::BackgroundRole) {
        if (isGroup) return QColor(42, 44, 56);
        if (isLabel) return QColor(52, 47, 18);
        if (isText)  return QColor(8, 48, 58);
        if (m_visibleRows.value(idx.row(), -1) == m_list->playheadIndex())
            return QColor(200, 160, 30, 55);
        if (cue->userColor().isValid()) return cue->userColor();
        if (!cue->parentGroupId().isEmpty()) return QColor(30, 38, 30);
    }

    if (idx.column() == ColTarget) {
        if (const auto *tc = dynamic_cast<const TextCue*>(cue)) {
            if (role == Qt::DisplayRole) {
                const QString t = tc->text();
                return t.isEmpty() ? QString("(nessun testo)") : t.left(40).replace('\n', ' ');
            }
            if (role == Qt::ForegroundRole)
                return tc->text().isEmpty() ? QColor(200, 50, 50) : QColor(130, 220, 240);
            return {};
        }
        if (const auto *ac = dynamic_cast<const AudioCue*>(cue)) {
            if (role == Qt::DisplayRole)
                return ac->filePath().isEmpty()
                    ? QString("✗ nessun file")
                    : QFileInfo(ac->filePath()).fileName();
            if (role == Qt::ForegroundRole)
                return ac->filePath().isEmpty() ? QColor(200, 50, 50) : QColor(160, 210, 160);
            if (role == Qt::ToolTipRole)
                return ac->filePath();
            return {};
        }
        if (const auto *vc = dynamic_cast<const VideoCue*>(cue)) {
            if (role == Qt::DisplayRole)
                return vc->filePath().isEmpty()
                    ? QString("✗ nessun file")
                    : QFileInfo(vc->filePath()).fileName();
            if (role == Qt::ForegroundRole)
                return vc->filePath().isEmpty() ? QColor(200, 50, 50) : QColor(160, 210, 160);
            if (role == Qt::ToolTipRole)
                return vc->filePath();
            return {};
        }
        const auto *cc = dynamic_cast<const ControlCue*>(cue);
        if (!cc) return {};

        if (role == Qt::DisplayRole) {
            if (cc->targetId().isEmpty())
                return QString("✗");
            Cue *t = m_list->findCueById(cc->targetId());
            if (!t) return QString("? non trovato");
            const QString num = t->number().isEmpty() ? "—" : t->number();
            const QString nm  = t->name().isEmpty()   ? "(senza nome)" : t->name();
            return QString("→ #%1 – %2").arg(num, nm);
        }
        if (role == Qt::ForegroundRole) {
            if (cc->targetId().isEmpty())
                return QColor(200, 50, 50);
            if (!m_list->findCueById(cc->targetId()))
                return QColor(220, 120, 30);
            return QColor(160, 210, 160);
        }
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignVCenter | Qt::AlignLeft);
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        switch (idx.column()) {
        case ColNumber:
        case ColPreWait:
        case ColDuration:
        case ColPostWait:
        case ColContinue:
            return int(Qt::AlignCenter);
        }
    }

    return {};
}

QVariant CueListModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColNumber:   return "#";
    case ColType:     return "Tipo";
    case ColName:     return "Nome";
    case ColPreWait:  return "Pre-wait";
    case ColDuration: return "Durata";
    case ColPostWait: return "Post-wait";
    case ColContinue: return "↳";
    case ColTarget:   return "Target";
    }
    return {};
}

Qt::ItemFlags CueListModel::flags(const QModelIndex &idx) const {
    if (!idx.isValid()) return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled
                    | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    switch (idx.column()) {
    case ColNumber:
    case ColName:
    case ColPreWait:
    case ColPostWait:
    case ColDuration:
        f |= Qt::ItemIsEditable;
        break;
    }
    return f;
}

bool CueListModel::setData(const QModelIndex &idx, const QVariant &value, int role) {
    if (role != Qt::EditRole) return false;
    Cue *cue = cueForRow(idx.row());
    if (!cue) return false;

    switch (idx.column()) {
    case ColNumber: {
        const QString s = value.toString().trimmed();
        if (s == cue->number()) return false;
        cue->setNumber(s);
        return true;
    }
    case ColName: {
        const QString s = value.toString();
        if (s == cue->name()) return false;
        cue->setName(s);
        return true;
    }
    case ColPreWait: {
        const double v = parseSeconds(value.toString());
        if (v < 0) return false;
        cue->setPreWait(v);
        return true;
    }
    case ColPostWait: {
        const double v = parseSeconds(value.toString());
        if (v < 0) return false;
        cue->setPostWait(v);
        return true;
    }
    case ColDuration: {
        const double v = parseSeconds(value.toString());
        if (v < 0) return false;
        if (auto *ac = dynamic_cast<AudioCue*>(cue)) {
            const double fileDur = ac->duration();
            ac->setTrimEnd(fileDur > 0.001 && v >= fileDur ? 0.0 : v);
            return true;
        }
        if (auto *fc = dynamic_cast<FadeCue*>(cue)) {
            fc->setFadeDuration(v);
            return true;
        }
        if (auto *ec = dynamic_cast<EffectCue*>(cue)) {
            ec->setEffectDuration(v);
            return true;
        }
        return false;
    }
    }
    return false;
}

void CueListModel::onCueAdded(int) {
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

void CueListModel::onCueRemoved(int) {
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

void CueListModel::onCueMoved(int, int) {
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

void CueListModel::onCueStateChanged(int index, Cue::State) {
    const int vis = m_visibleRows.indexOf(index);
    if (vis >= 0)
        emit dataChanged(this->index(vis, 0), this->index(vis, ColCount - 1));
}

void CueListModel::onCuePropertyChanged(int index) {
    const QVector<int> next = computeVisibleRows();
    if (next != m_visibleRows) {
        beginResetModel();
        m_visibleRows = next;
        endResetModel();
    } else {
        const int vis = m_visibleRows.indexOf(index);
        if (vis >= 0)
            emit dataChanged(this->index(vis, 0), this->index(vis, ColCount - 1));
    }
}

void CueListModel::onPlayheadChanged(int) {
    if (rowCount() == 0) return;
    emit dataChanged(this->index(0, 0), this->index(rowCount() - 1, ColCount - 1),
                     {Qt::BackgroundRole});
}
