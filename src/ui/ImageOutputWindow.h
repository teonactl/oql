#pragma once
#include "engine/ProjectionWarp.h"
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QMetaObject>
#include <QStringList>

class ImageCue;
class WarpCalibrationOverlay;

class ImageOutputWindow : public QWidget {
    Q_OBJECT
public:
    explicit ImageOutputWindow(QWidget *parent = nullptr);

    void showCue(ImageCue *cue);
    void clearImage();
    void toggleFullScreen();

    // Il warp non è posseduto da questa finestra: punta a quello del
    // progetto corrente (Workspace), condiviso con VideoOutputWindow — vedi
    // commento su Workspace::projectionWarp.
    void setProjectionWarp(ProjectionWarp *warp);
    void setCalibrating(bool on);
    bool isCalibrating() const;

signals:
    void calibrationEdited();
    void calibrationCommitted();

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;
    void resizeEvent(QResizeEvent *event)           override;

private slots:
    void refresh();

private:
    QPixmap pixmapFor(const QString &path);
    QImage  composeFrame() const;

    ImageCue *m_cue = nullptr;
    QMetaObject::Connection m_connection;
    // E11: niente limite di dimensione — ogni path mai mostrato restava in
    // cache per sempre (una QPixmap intera a piena risoluzione per voce).
    // Cache bounded in stile LRU: m_pixmapOrder tiene l'ordine di accesso,
    // le voci più vecchie vengono espulse oltre kMaxCachedPixmaps.
    static constexpr int kMaxCachedPixmaps = 48;
    QHash<QString, QPixmap> m_pixmapCache;
    QStringList m_pixmapOrder;

    ProjectionWarp          *m_warp = nullptr;
    WarpCalibrationOverlay   *m_calibOverlay;
};
