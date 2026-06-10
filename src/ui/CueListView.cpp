#include "CueListView.h"
#include "CueListModel.h"
#include "engine/ControlCues.h"
#include "engine/AppSettings.h"
#include "engine/Cue.h"
#include <QHeaderView>
#include <QMenu>
#include <QPixmap>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QDataStream>
#include <QResizeEvent>
#include <QTimer>

CueListView::CueListView(CueListModel *model, QWidget *parent)
    : QTableView(parent), m_model(model)
{
    setModel(model);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setAlternatingRowColors(true);
    setShowGrid(false);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(AppSettings::instance().cueListRowHeight());
    {
        QFont f = font();
        const QString fam = AppSettings::instance().cueListFontFamily();
        if (!fam.isEmpty()) f.setFamily(fam);
        f.setPointSize(AppSettings::instance().cueListFontSize());
        setFont(f);
    }

    auto *h = horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setMinimumSectionSize(24);

    const QList<int> defaults = { 50, 60, 220, 200, 70, 70, 70, 30 };
    const QList<int> saved    = AppSettings::instance().cueListColumnWidths();
    const QList<int> &widths  = (saved.size() == CueListModel::ColCount) ? saved : defaults;
    for (int c = 0; c < CueListModel::ColCount; ++c)
        setColumnWidth(c, widths[c]);

    setDragEnabled(false);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDragDropOverwriteMode(false);

    connect(h, &QHeaderView::sectionResized, this, [this](int, int, int) {
        stretchFlexColumns();
        saveColumnWidths();
    });
}

void CueListView::resizeEvent(QResizeEvent *event) {
    QTableView::resizeEvent(event);
    stretchFlexColumns();
}

void CueListView::saveColumnWidths() {
    auto *h = horizontalHeader();
    QList<int> widths;
    for (int c = 0; c < CueListModel::ColCount; ++c)
        widths.append(h->sectionSize(c));
    AppSettings::instance().setCueListColumnWidths(widths);
}

void CueListView::applyRowHeight() {
    verticalHeader()->setDefaultSectionSize(AppSettings::instance().cueListRowHeight());
}

void CueListView::applyFont() {
    QFont f = font();
    const QString family = AppSettings::instance().cueListFontFamily();
    if (!family.isEmpty())
        f.setFamily(family);
    f.setPointSize(AppSettings::instance().cueListFontSize());
    setFont(f);
}

void CueListView::stretchFlexColumns() {
    if (m_stretchGuard) return;
    m_stretchGuard = true;

    auto *h = horizontalHeader();
    int fixed = 0;
    for (int c = 0; c < CueListModel::ColCount; ++c) {
        if (h->isSectionHidden(c)) continue;
        if (c == CueListModel::ColName || c == CueListModel::ColTarget) continue;
        fixed += h->sectionSize(c);
    }

    const int avail = viewport()->width() - fixed;
    if (avail > 0) {
        const int nameW   = h->sectionSize(CueListModel::ColName);
        const int targetW = h->sectionSize(CueListModel::ColTarget);
        const int total   = nameW + targetW;
        if (total > 0) {
            const int newName   = qMax(60, avail * nameW / total);
            const int newTarget = qMax(60, avail - newName);
            h->resizeSection(CueListModel::ColName,   newName);
            h->resizeSection(CueListModel::ColTarget, newTarget);
        }
    }

    m_stretchGuard = false;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

QVector<int> CueListView::validTargetRows(int srcRow) const {
    QVector<int> rows;
    Cue *srcCue = m_model->cueForRow(srcRow);
    const bool srcIsMedia = srcCue
        && (srcCue->type() == Cue::Type::Audio || srcCue->type() == Cue::Type::Video);

    const bool srcIsAudio = srcCue && srcCue->type() == Cue::Type::Audio;
    for (int row = 0; row < model()->rowCount(); ++row) {
        if (row == srcRow) continue;
        Cue *dstCue = m_model->cueForRow(row);
        if (!dynamic_cast<ControlCue*>(dstCue)) continue;
        const bool mediaOnly = (dstCue->type() == Cue::Type::Play
                             || dstCue->type() == Cue::Type::Speed);
        const bool audioOnly = (dstCue->type() == Cue::Type::Effect
                             || dstCue->type() == Cue::Type::ResetEffect);
        if ((!mediaOnly || srcIsMedia) && (!audioOnly || srcIsAudio))
            rows.append(row);
    }
    return rows;
}

QVector<int> CueListView::validGroupRows(int srcRow) const {
    Cue *srcCue = m_model->cueForRow(srcRow);
    if (!srcCue || srcCue->type() == Cue::Type::Group) return {};
    QVector<int> rows;
    for (int row = 0; row < model()->rowCount(); ++row) {
        if (row == srcRow) continue;
        Cue *c = m_model->cueForRow(row);
        if (c && c->type() == Cue::Type::Group)
            rows.append(row);
    }
    return rows;
}

static bool isOnRowCenter(const QRect &rowRect, int posY) {
    const int edge = rowRect.height() * 15 / 100;  // outer 15% = reorder zone
    return posY >= rowRect.top() + edge && posY <= rowRect.bottom() - edge;
}

// ── Editor navigation ─────────────────────────────────────────────────────────

// Tab/Shift+Tab strategy:
//   closeEditor  — close with NoHint (skip Qt's EditNextItem pipeline entirely),
//                  move selection to next/prev row, set m_editOnCurrentChange.
//   currentChanged — fires synchronously from setCurrentIndex; if flag is set,
//                  calls edit() on the new cell.  edit() is called AFTER the
//                  base has fully settled (state=NoState, selection updated),
//                  so the editor opens and keeps focus reliably every iteration.
// Tab/Shift+Tab: use NoUpdate when setting current to avoid selectionChanged,
// which triggers onSelectionChanged → setPlayhead → playheadChanged → selectRow,
// a chain that internally calls setCurrentIndex with the wrong column before
// our currentChanged slot fires.  With NoUpdate only currentChanged fires,
// keeping the column intact.  edit() in currentChanged then updates the selection.
void CueListView::closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) {
    if (hint == QAbstractItemDelegate::EditNextItem
     || hint == QAbstractItemDelegate::EditPreviousItem) {
        const QModelIndex cur = currentIndex();
        QAbstractItemView::closeEditor(editor, QAbstractItemDelegate::NoHint);
        const int nextRow = (hint == QAbstractItemDelegate::EditNextItem)
                            ? cur.row() + 1 : cur.row() - 1;
        if (nextRow >= 0 && nextRow < model()->rowCount()) {
            m_editOnCurrentChange = true;
            selectionModel()->setCurrentIndex(
                model()->index(nextRow, cur.column()),
                QItemSelectionModel::NoUpdate);
        }
        return;
    }
    QAbstractItemView::closeEditor(editor, hint);
}

void CueListView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
    QTableView::currentChanged(current, previous);
    if (m_editOnCurrentChange) {
        m_editOnCurrentChange = false;
        edit(current);  // edit() calls select() internally, updating the row highlight
    }
}

QModelIndex CueListView::moveCursor(CursorAction action, Qt::KeyboardModifiers mods) {
    return QTableView::moveCursor(action, mods);
}

// ── Context menu / keyboard ───────────────────────────────────────────────────

void CueListView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    const auto selIdxs = selectionModel()->selectedRows();
    if (selIdxs.size() >= 2) {
        QVector<int> rows;
        for (const auto &idx : selIdxs) rows.append(idx.row());
        std::sort(rows.begin(), rows.end());
        menu.addAction(QString("Raggruppa %1 selezionate").arg(rows.size()),
                       this, [this, rows]() { emit groupSelectionRequested(rows); });
        menu.addSeparator();
    }

    menu.addAction("Aggiungi Audio Cue",     this, &CueListView::addAudioRequested);
    menu.addAction("Aggiungi Video Cue",     this, &CueListView::addVideoRequested);
    menu.addSeparator();
    menu.addAction("Aggiungi Stop Cue",      this, &CueListView::addStopRequested);
    menu.addAction("Aggiungi Fade Cue",      this, &CueListView::addFadeRequested);
    menu.addAction("Aggiungi Pause Cue",     this, &CueListView::addPauseRequested);
    menu.addAction("Aggiungi Microfono Cue", this, &CueListView::addMicRequested);
    menu.addSeparator();
    menu.addAction("Aggiungi Gruppo",               this, &CueListView::addGroupRequested);
    menu.addAction("Aggiungi Etichetta",            this, &CueListView::addLabelRequested);
    menu.addAction("Aggiungi Testo",                this, &CueListView::addTextRequested);
    menu.addSeparator();
    menu.addAction("Aggiungi Effect Cue",           this, &CueListView::addEffectRequested);
    menu.addAction("Aggiungi Reset Effetti Cue",    this, &CueListView::addResetEffectRequested);
    menu.addAction("Aggiungi Script Cue",           this, &CueListView::addScriptRequested);
    menu.addSeparator();
    // Color palette submenu — visible only when at least one row is selected
    if (!selIdxs.isEmpty()) {
        static const struct { const char *name; QColor color; } kPalette[] = {
            { "Rosa",     QColor(255, 175, 185) },
            { "Pesca",    QColor(255, 210, 170) },
            { "Giallo",   QColor(255, 240, 155) },
            { "Verde",    QColor(185, 240, 185) },
            { "Menta",    QColor(170, 235, 215) },
            { "Celeste",  QColor(170, 215, 255) },
            { "Azzurro",  QColor(185, 195, 255) },
            { "Viola",    QColor(215, 185, 255) },
            { "Lilla",    QColor(240, 185, 255) },
            { "Grigio",   QColor(215, 215, 220) },
        };

        QMenu *colorMenu = menu.addMenu("Colore");

        auto makeIcon = [](QColor c) {
            QPixmap px(14, 14);
            px.fill(c);
            return QIcon(px);
        };

        QVector<int> targetRows;
        for (const auto &idx : selIdxs) targetRows.append(idx.row());

        for (const auto &entry : kPalette) {
            QColor c = entry.color;
            colorMenu->addAction(makeIcon(c), entry.name, this, [this, targetRows, c]() {
                for (int row : targetRows)
                    if (Cue *cue = m_model->cueForRow(row)) cue->setUserColor(c);
            });
        }
        colorMenu->addSeparator();
        colorMenu->addAction("Nessuno", this, [this, targetRows]() {
            for (int row : targetRows)
                if (Cue *cue = m_model->cueForRow(row)) cue->setUserColor(QColor{});
        });
        menu.addSeparator();
    }
    menu.addAction("Elimina cue",                   this, &CueListView::deleteRequested);
    menu.exec(event->globalPos());
}

void CueListView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        return;
    }
    if (event->key() == Qt::Key_F2) {
        const QModelIndex cur = currentIndex();
        if (cur.isValid()) { edit(cur); return; }
    }
    if (event->key() == Qt::Key_G && (event->modifiers() & Qt::ControlModifier)) {
        const auto selIdxs = selectionModel()->selectedRows();
        if (selIdxs.size() >= 2) {
            QVector<int> rows;
            for (const auto &idx : selIdxs) rows.append(idx.row());
            std::sort(rows.begin(), rows.end());
            emit groupSelectionRequested(rows);
            return;
        }
    }
    QTableView::keyPressEvent(event);
}

void CueListView::mouseDoubleClickEvent(QMouseEvent *event) {
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) return;

    Cue *cue = m_model->cueForRow(idx.row());
    if (!cue) return;

    if (cue->type() == Cue::Type::Group) {
        emit groupToggleRequested(idx.row());
        return;
    }

    emit cueDoubleClicked(idx.row());
}

// ── Mouse: custom drag initiation ────────────────────────────────────────────

void CueListView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const QModelIndex idx = indexAt(event->pos());
        m_dragRow      = idx.isValid() ? idx.row() : -1;
        m_dragStartPos = event->pos();
    }
    QTableView::mousePressEvent(event);
}

void CueListView::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || m_dragRow < 0) {
        QTableView::mouseMoveEvent(event);
        return;
    }
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        QTableView::mouseMoveEvent(event);
        return;
    }

    // Collect all selected rows; if dragRow is among them use them all, else drag just dragRow
    const auto selIdxs = selectionModel()->selectedRows();
    QVector<int> dragRows;
    for (const auto &idx : selIdxs) dragRows.append(idx.row());
    if (!dragRows.contains(m_dragRow)) dragRows = { m_dragRow };
    std::sort(dragRows.begin(), dragRows.end());

    const bool isMulti = dragRows.size() > 1;
    if (isMulti) {
        // Multi-drag: only group rows are valid targets
        m_validTargetRows.clear();
        m_validGroupRows.clear();
        for (int row = 0; row < model()->rowCount(); ++row) {
            if (dragRows.contains(row)) continue;
            Cue *c = m_model->cueForRow(row);
            if (c && c->type() == Cue::Type::Group)
                m_validGroupRows.append(row);
        }
    } else {
        m_validTargetRows = validTargetRows(m_dragRow);
        m_validGroupRows  = validGroupRows(m_dragRow);
    }
    viewport()->update();

    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << dragRows;

    auto *mime = new QMimeData;
    mime->setData(kMime, payload);

    const int  lastCol = model()->columnCount() - 1;
    const QRect rowRect = visualRect(model()->index(m_dragRow, 0))
                          .united(visualRect(model()->index(m_dragRow, lastCol)));
    QPixmap px = viewport()->grab(rowRect);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!px.isNull()) {
        drag->setPixmap(px);
        drag->setHotSpot(event->pos() - rowRect.topLeft());
    }

    drag->exec(Qt::MoveAction);  // blocking — dropEvent runs inside here

    m_dragRow            = -1;
    m_dropHighlightRow   = -1;
    m_groupDropHighlight = -1;
    m_validTargetRows.clear();
    m_validGroupRows.clear();
    viewport()->update();
}

void CueListView::mouseReleaseEvent(QMouseEvent *event) {
    m_dragRow = -1;
    QTableView::mouseReleaseEvent(event);
}

// ── Paint: highlight valid target rows during drag ────────────────────────────

void CueListView::paintEvent(QPaintEvent *event) {
    QTableView::paintEvent(event);
    if ((m_validTargetRows.isEmpty() && m_validGroupRows.isEmpty()) || !model()) return;

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);
    const int lastCol = model()->columnCount() - 1;

    auto rowRect = [&](int row) {
        return visualRect(model()->index(row, 0))
               .united(visualRect(model()->index(row, lastCol)));
    };

    // Blue highlight: control-cue target rows
    for (int row : m_validTargetRows) {
        const QRect r = rowRect(row);
        if (!r.isValid()) continue;
        if (row == m_dropHighlightRow) {
            p.setPen(QPen(QColor(60, 160, 255), 2));
            p.setBrush(QColor(60, 160, 255, 55));
        } else {
            p.setPen(QPen(QColor(60, 160, 255, 120), 1, Qt::DashLine));
            p.setBrush(QColor(60, 160, 255, 18));
        }
        p.drawRect(r.adjusted(1, 1, -1, -1));
    }

    // Green highlight: group rows (drag-to-assign)
    for (int row : m_validGroupRows) {
        const QRect r = rowRect(row);
        if (!r.isValid()) continue;
        if (row == m_groupDropHighlight) {
            p.setPen(QPen(QColor(60, 210, 110), 2));
            p.setBrush(QColor(60, 210, 110, 60));
        } else {
            p.setPen(QPen(QColor(60, 210, 110, 130), 1, Qt::DashLine));
            p.setBrush(QColor(60, 210, 110, 18));
        }
        p.drawRect(r.adjusted(1, 1, -1, -1));
    }
}

// ── Drag ─────────────────────────────────────────────────────────────────────

void CueListView::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(kMime))
        event->acceptProposedAction();
    else
        event->ignore();
}

void CueListView::dragMoveEvent(QDragMoveEvent *event) {
    if (!event->mimeData()->hasFormat(kMime)) {
        event->ignore();
        return;
    }

    const QPoint      pos  = event->position().toPoint();
    const QModelIndex dest = indexAt(pos);

    int newHighlight      = -1;
    int newGroupHighlight = -1;
    if (dest.isValid() && dest.row() != m_dragRow) {
        const QRect rowRect = visualRect(model()->index(dest.row(), 0));
        const bool  onCenter = isOnRowCenter(rowRect, pos.y());
        if (onCenter || dest.column() == CueListModel::ColTarget) {
            if (m_validTargetRows.contains(dest.row()))
                newHighlight = dest.row();
        }
        if (onCenter && m_validGroupRows.contains(dest.row()))
            newGroupHighlight = dest.row();
    }

    bool changed = (newHighlight != m_dropHighlightRow)
                || (newGroupHighlight != m_groupDropHighlight);
    m_dropHighlightRow   = newHighlight;
    m_groupDropHighlight = newGroupHighlight;
    if (changed) viewport()->update();

    event->acceptProposedAction();
}

void CueListView::dragLeaveEvent(QDragLeaveEvent *event) {
    m_dropHighlightRow   = -1;
    m_groupDropHighlight = -1;
    viewport()->update();
    QTableView::dragLeaveEvent(event);
}

// ── Drop ─────────────────────────────────────────────────────────────────────

void CueListView::dropEvent(QDropEvent *event) {
    m_dropHighlightRow = -1;

    if (!event->mimeData()->hasFormat(kMime)) {
        event->ignore();
        viewport()->update();
        return;
    }

    QByteArray payload = event->mimeData()->data(kMime);
    QDataStream ds(&payload, QIODevice::ReadOnly);
    QVector<int> srcRows;
    ds >> srcRows;
    if (srcRows.isEmpty()) {
        event->ignore();
        viewport()->update();
        return;
    }

    const QPoint      pos  = event->position().toPoint();
    const QModelIndex dest = indexAt(pos);

    auto clearDragState = [this]() {
        m_dragRow = -1; m_dropHighlightRow = -1; m_groupDropHighlight = -1;
        m_validTargetRows.clear(); m_validGroupRows.clear();
    };

    // ── Multi-row drop ────────────────────────────────────────────────────────
    if (srcRows.size() > 1) {
        if (dest.isValid() && m_validGroupRows.contains(dest.row())) {
            const int grp = dest.row();
            clearDragState();
            emit multiGroupAssignRequested(srcRows, grp);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            viewport()->update();
            return;
        }
        // Multi-drag not over a valid group: abort
        clearDragState();
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        viewport()->update();
        return;
    }

    // ── Single-row drop ───────────────────────────────────────────────────────
    const int srcRow = srcRows.first();

    if (dest.isValid() && dest.row() != srcRow) {
        // Group assignment
        if (m_validGroupRows.contains(dest.row())) {
            const int grp = dest.row();
            clearDragState();
            emit groupAssignRequested(srcRow, grp);
            selectRow(srcRow);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            viewport()->update();
            return;
        }

        // Target assignment — center zone or explicit Target column
        const bool onTgtCol = dest.column() == CueListModel::ColTarget;
        const QRect rowRect = visualRect(model()->index(dest.row(), 0));
        const bool onCenter = isOnRowCenter(rowRect, pos.y());
        if ((onCenter || onTgtCol) && m_validTargetRows.contains(dest.row())) {
            const int tgt = dest.row();
            clearDragState();
            emit targetAssignRequested(srcRow, tgt);
            selectRow(tgt);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            viewport()->update();
            return;
        }
    }

    // Normal reorder (or ungroup if dragging a child outside its group)
    int destRow;
    if (!dest.isValid()) {
        destRow = model()->rowCount();
    } else {
        destRow = dest.row();
        const QRect rect = visualRect(dest);
        if (pos.y() > rect.center().y())
            destRow++;
    }

    int targetRow = destRow;
    if (srcRow < destRow) targetRow--;
    clearDragState();
    targetRow = qBound(0, targetRow, model()->rowCount() - 1);

    Cue *srcCue = m_model->cueForRow(srcRow);
    const bool isChild = srcCue && !srcCue->parentGroupId().isEmpty();

    if (isChild) {
        Cue *dstCue = m_model->cueForRow(targetRow);
        const bool staysInGroup = dstCue && (
            (dstCue->type() == Cue::Type::Group && dstCue->id() == srcCue->parentGroupId()) ||
            (dstCue->parentGroupId() == srcCue->parentGroupId())
        );
        if (staysInGroup) {
            if (srcRow != targetRow) emit moveRequested(srcRow, targetRow);
        } else {
            emit ungroupRequested(srcRow, targetRow);
        }
    } else if (srcRow != targetRow) {
        emit moveRequested(srcRow, targetRow);
    }

    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    viewport()->update();
}
