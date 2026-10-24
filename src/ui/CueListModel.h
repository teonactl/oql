#pragma once
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QString>
#include "engine/CueList.h"

class RecordCue;

class CueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColNumber   = 0,
        ColType,
        ColName,
        ColTarget,     // resolved target name for control cues
        ColOutput,     // physical output device (AudioCue only, "" = predefinita)
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
    void         ensureCueByIdCache() const;
    void         ensureRecordSrcCache() const;

    CueList      *m_list;
    QVector<int>  m_visibleRows;

    // E3: onCuePropertyChanged() riceve solo l'indice della cue cambiata, non
    // quale proprietà — invece di ricalcolare sempre computeVisibleRows()
    // (O(n log n) con allocazioni QSet/QMap, ad OGNI property changed di
    // QUALSIASI cue), si tiene traccia del parentGroupId con cui ciascuna
    // cue è stata vista l'ultima volta: solo quello incide sulla disposizione
    // visibile (il collapse dei gruppi arriva già separatamente via
    // cueLayoutChanged). Se non è cambiato, si salta il recompute.
    mutable QVector<QString> m_lastParentGroupId;

    // E2: onPlayheadChanged() riceveva solo l'indice e ridisegnava TUTTE le
    // righe per aggiornare l'evidenziazione di una sola; si tiene traccia
    // dell'ultima riga visibile evidenziata per limitare il dataChanged a
    // quella + quella nuova.
    int m_lastPlayheadVisibleRow = -1;

    // E1: la colonna Target per AudioCue/ControlCue faceva una scansione
    // O(n) della lista per ogni singola cella ad ogni repaint (ricerca per
    // id, o ricerca della RecordCue collegata) — con N righe questo è
    // O(n^2) per un repaint completo. Cache invalidate in modo mirato:
    // m_cueById solo sui cambi strutturali (gli id sono immutabili), la
    // mappa RecordCue solo sui cambi strutturali o quando la cue cambiata è
    // proprio una RecordCue (potrebbe aver cambiato linkedAudioCueId).
    mutable QHash<QString, Cue*>       m_cueById;
    mutable bool                       m_cueByIdDirty = true;
    mutable QHash<QString, RecordCue*> m_recordSrcByAudioCueId;
    mutable bool                       m_recordSrcCacheDirty = true;
};
