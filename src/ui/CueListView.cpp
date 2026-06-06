#include "CueListView.h"
#include "CueListModel.h"
#include "engine/ControlCues.h"
#include <QHeaderView>
#include <QMenu>
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

CueListView::CueListView(CueListModel *model, QWidget *parent)
    : QTableView(parent), m_model(model)
{
    setModel(model);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    setAlternatingRowColors(true);
    setShowGrid(false);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(28);

    auto *h = horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setMinimumSectionSize(24);

    setColumnWidth(CueListModel::ColNumber,   50);
    setColumnWidth(CueListModel::ColType,     60);
    setColumnWidth(CueListModel::ColName,     220);
    setColumnWidth(CueListModel::ColTarget,   200);
    setColumnWidth(CueListModel::ColPreWait,  70);
    setColumnWidth(CueListModel::ColDuration, 70);
    setColumnWidth(CueListModel::ColPostWait, 70);
    setColumnWidth(CueListModel::ColContinue, 30);

    setDragEnabled(false);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDragDropOverwriteMode(false);

    connect(h, &QHeaderView::sectionResized, this, [this](int, int, int) {
        stretchFlexColumns();
    });
}

void CueListView::resizeEvent(QResizeEvent *event) {
    QTableView::resizeEvent(event);
    stretchFlexColumns();
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

    for (int row = 0; row < model()->rowCount(); ++row) {
        if (row == srcRow) continue;
        Cue *dstCue = m_model->cueForRow(row);
        if (!dynamic_cast<ControlCue*>(dstCue)) continue;
        const bool mediaOnly = (dstCue->type() == Cue::Type::Play
                             || dstCue->type() == Cue::Type::Speed);
        if (!mediaOnly || srcIsMedia)
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

// ── Context menu / keyboard ───────────────────────────────────────────────────

void CueListView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.addAction("Aggiungi Audio Cue",     this, &CueListView::addAudioRequested);
    menu.addAction("Aggiungi Video Cue",     this, &CueListView::addVideoRequested);
    menu.addSeparator();
    menu.addAction("Aggiungi Stop Cue",      this, &CueListView::addStopRequested);
    menu.addAction("Aggiungi Fade Cue",      this, &CueListView::addFadeRequested);
    menu.addAction("Aggiungi Pause Cue",     this, &CueListView::addPauseRequested);
    menu.addAction("Aggiungi Microfono Cue", this, &CueListView::addMicRequested);
    menu.addSeparator();
    menu.addAction("Aggiungi Gruppo",        this, &CueListView::addGroupRequested);
    menu.addAction("Aggiungi Etichetta",     this, &CueListView::addLabelRequested);
    menu.addAction("Aggiungi Testo",         this, &CueListView::addTextRequested);
    menu.addSeparator();
    menu.addAction("Elimina cue",            this, &CueListView::deleteRequested);
    menu.exec(event->globalPos());
}

void CueListView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        return;
    }
    if (event->key() == Qt::Key_F2) {
        const QModelIndex cur = currentIndex();
        if (cur.isValid()) {
            edit(cur);
            return;
        }
    }
    QTableView::keyPressEvent(event);
}

void CueListView::mouseDoubleClickEvent(QMouseEvent *event) {
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) return;
    const int col = idx.column();

    // Toggle group collapse on double-click anywhere on the row
    Cue *cue = m_model->cueForRow(idx.row());
    if (cue && cue->type() == Cue::Type::Group) {
        emit groupToggleRequested(idx.row());
        return;
    }

    // Inline editing columns
    if (col == CueListModel::ColNumber
        || col == CueListModel::ColName
        || col == CueListModel::ColPreWait
        || col == CueListModel::ColDuration
        || col == CueListModel::ColPostWait) {
        QTableView::mouseDoubleClickEvent(event);
        return;
    }

    // File picker for audio/video Target cell
    if (col == CueListModel::ColTarget) {
        Cue *cue = m_model->cueForRow(idx.row());
        if (cue && (cue->type() == Cue::Type::Audio || cue->type() == Cue::Type::Video)) {
            emit filePickRequested(idx.row());
            return;
        }
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

    m_validTargetRows = validTargetRows(m_dragRow);
    m_validGroupRows  = validGroupRows(m_dragRow);
    viewport()->update();

    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << m_dragRow;

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

    // Always read srcRow from MIME data — on X11/Wayland mouseReleaseEvent
    // fires before dropEvent and resets m_dragRow to -1 before we get here.
    QByteArray payload = event->mimeData()->data(kMime);
    QDataStream ds(&payload, QIODevice::ReadOnly);
    int srcRow = -1;
    ds >> srcRow;
    if (srcRow < 0) {
        event->ignore();
        viewport()->update();
        return;
    }

    const QPoint      pos  = event->position().toPoint();
    const QModelIndex dest = indexAt(pos);

    // Re-evaluate at drop time using m_validGroupRows / m_validTargetRows directly.
    // dragLeaveEvent fires just before dropEvent on X11 and clears m_groupDropHighlight
    // and m_dropHighlightRow, so we cannot rely on those saved highlights here.
    // m_validGroupRows and m_validTargetRows are NOT cleared by dragLeaveEvent.
    if (dest.isValid() && dest.row() != srcRow) {

        // Group assignment — any position over a valid group row counts at drop time
        if (m_validGroupRows.contains(dest.row())) {
            const int grp = dest.row();
            m_dragRow            = -1;
            m_dropHighlightRow   = -1;
            m_groupDropHighlight = -1;
            m_validTargetRows.clear();
            m_validGroupRows.clear();
            emit groupAssignRequested(srcRow, grp);
            selectRow(srcRow);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            viewport()->update();
            return;
        }

        // Target assignment — center zone or explicit Target column
        const bool onTgtCol  = dest.column() == CueListModel::ColTarget;
        const QRect rowRect  = visualRect(model()->index(dest.row(), 0));
        const bool  onCenter = isOnRowCenter(rowRect, pos.y());
        if ((onCenter || onTgtCol) && m_validTargetRows.contains(dest.row())) {
            const int tgt = dest.row();
            m_dragRow            = -1;
            m_dropHighlightRow   = -1;
            m_groupDropHighlight = -1;
            m_validTargetRows.clear();
            m_validGroupRows.clear();
            emit targetAssignRequested(srcRow, tgt);
            selectRow(tgt);
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            viewport()->update();
            return;
        }
    }

    // Normal reorder
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

    const int src = srcRow;
    m_dragRow            = -1;
    m_dropHighlightRow   = -1;
    m_groupDropHighlight = -1;
    m_validTargetRows.clear();
    m_validGroupRows.clear();

    if (src != targetRow) {
        targetRow = qBound(0, targetRow, model()->rowCount() - 1);
        emit moveRequested(src, targetRow);
    }

    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    viewport()->update();
}
