#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include "engine/CueList.h"

class CueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColNumber   = 0,
        ColType,
        ColName,
        ColTarget,     // resolved target name for control cues
        ColPreWait,
        ColDuration,
        ColPostWait,
        ColContinue,
        ColCount
    };

    explicit CueListModel(CueList *list, QObject *parent = nullptr);

    int     rowCount   (const QModelIndex &p = {}) const override;
    int     columnCount(const QModelIndex &p = {}) const override;
    QVariant data      (const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    bool     setData   (const QModelIndex &idx, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &idx) const override;

    Cue* cueForRow(int row) const;
    int  actualRowForVisible(int visibleRow) const { return m_visibleRows.value(visibleRow, -1); }
    int  visibleRowForActual(int actualIndex) const { return m_visibleRows.indexOf(actualIndex); }

    void toggleGroupAt(int visibleRow);

private slots:
    void onCueAdded(int index);
    void onCueRemoved(int index);
    void onCueMoved(int from, int to);
    void onCueStateChanged(int index, Cue::State state);
    void onCuePropertyChanged(int index);
    void onPlayheadChanged(int index);

private:
    QVector<int> computeVisibleRows() const;
    void         rebuildVisibleRows();

    CueList      *m_list;
    QVector<int>  m_visibleRows;
};
