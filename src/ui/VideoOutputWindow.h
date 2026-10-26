#pragma once
#include "engine/ProjectionWarp.h"
#include <QWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImage>
#include <QHash>
#include <QPixmap>

class WarpCalibrationOverlay;

class VideoOutputWindow : public QWidget {
    Q_OBJECT
public:
    enum class Background { Black, White, Image };

    explicit VideoOutputWindow(QWidget *parent = nullptr);

    // Sink da passare a VideoCue::setVideoSink(): i frame arrivano qui
    // invece che in un QVideoWidget, perché serve intercettarli per
    // comporli e deformarli (warp) prima di disegnarli — un QVideoWidget
    // normale non permette trasformazioni prospettiche per-cella.
    QVideoSink* videoSink() { return m_sink; }

    void setOpacity(double level);
    void setBackground(Background mode, const QString &imagePath);
    void toggleFullScreen();

    // Il warp non è posseduto da questa finestra: punta a quello del
    // progetto corrente (Workspace), condiviso con ImageOutputWindow.
    void setProjectionWarp(ProjectionWarp *warp);
    void setCalibrating(bool on);
    bool isCalibrating() const;

signals:
    void calibrationEdited();   // live, durante il trascinamento
    void calibrationCommitted(); // al rilascio: da persistere sul progetto

protected:
    void paintEvent(QPaintEvent *event)       override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)            override;
    void resizeEvent(QResizeEvent *event)           override;

private slots:
    void onVideoFrameChanged(const QVideoFrame &frame);

private:
    QImage composeFrame() const;

    QVideoSink *m_sink;
    QImage      m_lastFrame; // ultimo frame video decodificato (può essere nullo)

    double      m_opacity = 1.0; // 1 = pieno, 0 = nero/bianco/immagine di fade
    Background  m_bgMode = Background::Black;
    QString     m_bgImagePath;
    QHash<QString, QPixmap> m_pixmapCache;

    ProjectionWarp          *m_warp = nullptr;
    WarpCalibrationOverlay   *m_calibOverlay;
};
