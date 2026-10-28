#pragma once
#include "engine/VirtualScreen.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QTimer>

class Workspace;
class ImageCue;

// Stesso schema di VideoOutputWindow (vedi il commento lì): nessuna cue
// "assegnata" dall'esterno — a ogni ridisegno si scorrono gli VirtualScreen
// del PhysicalOutput e per ciascuno si chiede alla CueList quale ImageCue è
// in riproduzione lì (CueList::activeImageCueForScreen).
class ImageOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit ImageOutputWindow(QWidget *parent = nullptr, bool embedded = false);

    void setOutput(Workspace *workspace, const QString &physicalOutputId);
    void toggleFullScreen();

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event)        override;
    void mouseMoveEvent(QMouseEvent *event)         override;
    void mouseReleaseEvent(QMouseEvent *event)      override;
    void keyPressEvent(QKeyEvent *event)            override;
    void showEvent(QShowEvent *event)               override;
    void hideEvent(QHideEvent *event)               override;

private:
    QPixmap pixmapFor(const QString &path) const;
    QImage  composeCueFrame(ImageCue *cue) const;
    QImage  composeCanvas() const;

    Workspace *m_workspace = nullptr;
    QString    m_outputId;
    QTimer    *m_repaintTimer;

    // E11 (vedi versione precedente): cache LRU bounded, non illimitata.
    static constexpr int kMaxCachedPixmaps = 48;
    mutable QHash<QString, QPixmap> m_pixmapCache;
    mutable QStringList             m_pixmapOrder;
    bool m_embedded = false;

    // Trascinamento manuale della finestra — vedi lo stesso commento in
    // VideoOutputWindow.h sul perché window() invece di this.
    bool   m_draggingWindow = false;
    QPoint m_dragStartGlobalPos;
    QPoint m_dragStartWindowPos;
};
