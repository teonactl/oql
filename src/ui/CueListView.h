#pragma once
#include <QTableView>
#include <QVector>
#include <QPoint>

class CueListModel;

class CueListView : public QTableView {
    Q_OBJECT
public:
    explicit CueListView(CueListModel *model, QWidget *parent = nullptr);

signals:
    void addAudioRequested();
    void addVideoRequested();
    void addStopRequested();
    void addFadeRequested();
    void addPauseRequested();
    void addMicRequested();
    void addGroupRequested();
    void addLabelRequested();
    void addTextRequested();
    void addEffectRequested();
    void addResetEffectRequested();
    void deleteRequested();
    void cueDoubleClicked(int row);
    void filePickRequested(int row);
    void moveRequested(int from, int to);
    void targetAssignRequested(int sourceCueRow, int controlCueRow);
    void groupToggleRequested(int row);
    void groupAssignRequested(int cueRow, int groupRow);
    void ungroupRequested(int cueVisRow, int destVisRow);
    void groupSelectionRequested(QVector<int> visRows);
    void multiGroupAssignRequested(QVector<int> visRows, int groupVisRow);

protected:
    QModelIndex moveCursor(CursorAction action, Qt::KeyboardModifiers mods) override;
    void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
    void contextMenuEvent(QContextMenuEvent *event)   override;
    void keyPressEvent(QKeyEvent *event)              override;
    void mouseDoubleClickEvent(QMouseEvent *event)    override;
    void mousePressEvent(QMouseEvent *event)          override;
    void mouseMoveEvent(QMouseEvent *event)           override;
    void mouseReleaseEvent(QMouseEvent *event)        override;
    void paintEvent(QPaintEvent *event)               override;
    void dragEnterEvent(QDragEnterEvent *event)       override;
    void dragMoveEvent(QDragMoveEvent *event)         override;
    void dragLeaveEvent(QDragLeaveEvent *event)       override;
    void dropEvent(QDropEvent *event)                 override;
    void resizeEvent(QResizeEvent *event)             override;

public:
    void applyRowHeight();
    void applyFont();

private:
    void stretchFlexColumns();
    void saveColumnWidths();
    QVector<int> validTargetRows(int srcRow) const;
    QVector<int> validGroupRows(int srcRow)  const;

    static constexpr char kMime[] = "application/x-openqlab-cuerow";

    CueListModel *m_model;
    bool          m_stretchGuard         = false;
    bool          m_editOnCurrentChange  = false;
    int           m_dragRow              = -1;
    int           m_dropHighlightRow     = -1;
    int           m_groupDropHighlight   = -1;
    QVector<int>  m_validTargetRows;
    QVector<int>  m_validGroupRows;
    QPoint        m_dragStartPos;
};
